#include "emulator.h"

#include <Core/PS4/HLE/Graphics/graphics_render.h>
#include <Emulator/HLE/Libraries/LibPad/controller.h>
#include <Util/Singleton.h>
#include <vulkan_util.h>

#include "Core/PS4/HLE/Graphics/video_out.h"
#include "Emulator/HLE/Libraries/LibPad/pad.h"
#include "version.h"

namespace Emu {

bool m_emu_needs_exit = false;

void emuInit(u32 width, u32 height) {
    auto* window_ctx = Singleton<Emu::WindowCtx>::Instance();

    window_ctx->m_graphic_ctx.screen_width = width;
    window_ctx->m_graphic_ctx.screen_height = height;
}

void checkAndWaitForGraphicsInit() {
    auto* window_ctx = Singleton<Emu::WindowCtx>::Instance();
    Lib::LockMutexGuard lock(window_ctx->m_mutex);

    while (!window_ctx->m_is_graphic_initialized) {
        window_ctx->m_graphic_initialized_cond.WaitCondVar(&window_ctx->m_mutex);
    }
}
static void CreateSdlWindow(WindowCtx* ctx) {
    int width = static_cast<int>(ctx->m_graphic_ctx.screen_width);
    int height = static_cast<int>(ctx->m_graphic_ctx.screen_height);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("%s\n", SDL_GetError());
        std::exit(0);
    }
    std::string title = "shadps4 v" + std::string(Emulator::VERSION);
    ctx->m_window = SDL_CreateWindowWithPosition(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                                                 (static_cast<uint32_t>(SDL_WINDOW_HIDDEN) | static_cast<uint32_t>(SDL_WINDOW_VULKAN)));

    ctx->is_window_hidden = true;  // hide window until we need to show something (should draw something in buffers)

    if (ctx->m_window == nullptr) {
        printf("%s\n", SDL_GetError());
        std::exit(0);
    }

    SDL_SetWindowResizable(ctx->m_window, SDL_FALSE);  // we don't support resizable atm
}
void emuRun() {
    auto* window_ctx = Singleton<Emu::WindowCtx>::Instance();
    window_ctx->m_mutex.LockMutex();
    {
        // init window and wait until init finishes
        CreateSdlWindow(window_ctx);
        Graphics::Vulkan::vulkanCreate(window_ctx);
        window_ctx->m_is_graphic_initialized = true;
        window_ctx->m_graphic_initialized_cond.SignalCondVar();
    }
    window_ctx->m_mutex.UnlockMutex();

    bool exit_loop = false;

    for (;;) {
        if (exit_loop) {
            break;
        }

        SDL_Event event;
        if (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
                case SDL_EVENT_QUIT: m_emu_needs_exit = true; break;

                case SDL_EVENT_TERMINATING: m_emu_needs_exit = true; break;

                case SDL_EVENT_WILL_ENTER_BACKGROUND: break;

                case SDL_EVENT_DID_ENTER_BACKGROUND: break;

                case SDL_EVENT_WILL_ENTER_FOREGROUND: break;

                case SDL_EVENT_DID_ENTER_FOREGROUND: break;

                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP: keyboardEvent(&event); break;
            }
            continue;
        }
        exit_loop = m_emu_needs_exit;

        if (!exit_loop) {
            HLE::Libs::Graphics::VideoOut::videoOutFlip(100000);  // flip every 0.1 sec
        }
    }
    std::exit(0);
}

HLE::Libs::Graphics::GraphicCtx* getGraphicCtx() {
    auto* window_ctx = Singleton<Emu::WindowCtx>::Instance();
    Lib::LockMutexGuard lock(window_ctx->m_mutex);

    return &window_ctx->m_graphic_ctx;
}

void DrawBuffer(HLE::Libs::Graphics::VideoOutVulkanImage* image) {
    auto* window_ctx = Singleton<Emu::WindowCtx>::Instance();
    if (window_ctx->is_window_hidden) {
        SDL_ShowWindow(window_ctx->m_window);
        window_ctx->is_window_hidden = false;
    }

    window_ctx->swapchain->current_index = static_cast<u32>(-1);

    auto result = vkAcquireNextImageKHR(window_ctx->m_graphic_ctx.m_device, window_ctx->swapchain->swapchain, UINT64_MAX, nullptr,
                                        window_ctx->swapchain->present_complete_fence, &window_ctx->swapchain->current_index);

    if (result != VK_SUCCESS) {
        printf("Can't aquireNextImage\n");
        std::exit(0);
    }
    if (window_ctx->swapchain->current_index == static_cast<u32>(-1)) {
        printf("Unsupported:swapchain current index is -1\n");
        std::exit(0);
    }

    do {
        result = vkWaitForFences(window_ctx->m_graphic_ctx.m_device, 1, &window_ctx->swapchain->present_complete_fence, VK_TRUE, 100000000);
    } while (result == VK_TIMEOUT);
    if (result != VK_SUCCESS) {
        printf("vkWaitForFences is not success\n");
        std::exit(0);
    }

    vkResetFences(window_ctx->m_graphic_ctx.m_device, 1, &window_ctx->swapchain->present_complete_fence);

    auto* blt_src_image = image;
    auto* blt_dst_image = window_ctx->swapchain;

    if (blt_src_image == nullptr) {
        printf("blt_src_image is null\n");
        std::exit(0);
    }
    if (blt_dst_image == nullptr) {
        printf("blt_dst_image is null\n");
        std::exit(0);
    }

    GPU::CommandBuffer buffer(10);

    auto* vk_buffer = buffer.getPool()->buffers[buffer.getIndex()];

    buffer.begin();

    Graphics::Vulkan::vulkanBlitImage(&buffer, blt_src_image, blt_dst_image);

    VkImageMemoryBarrier pre_present_barrier{};
    pre_present_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre_present_barrier.pNext = nullptr;
    pre_present_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    pre_present_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    pre_present_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    pre_present_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    pre_present_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre_present_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre_present_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pre_present_barrier.subresourceRange.baseMipLevel = 0;
    pre_present_barrier.subresourceRange.levelCount = 1;
    pre_present_barrier.subresourceRange.baseArrayLayer = 0;
    pre_present_barrier.subresourceRange.layerCount = 1;
    pre_present_barrier.image = window_ctx->swapchain->swapchain_images[window_ctx->swapchain->current_index];
    vkCmdPipelineBarrier(vk_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &pre_present_barrier);

    buffer.end();
    buffer.executeWithSemaphore();

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.pNext = nullptr;
    present.swapchainCount = 1;
    present.pSwapchains = &window_ctx->swapchain->swapchain;
    present.pImageIndices = &window_ctx->swapchain->current_index;
    present.pWaitSemaphores = &buffer.getPool()->semaphores[buffer.getIndex()];
    present.waitSemaphoreCount = 1;
    present.pResults = nullptr;

    const auto& queue = window_ctx->m_graphic_ctx.queues[10];

    if (queue.mutex != nullptr) {
        printf("queue.mutexe is null\n");
        std::exit(0);
    }

    result = vkQueuePresentKHR(queue.vk_queue, &present);
    if (result != VK_SUCCESS) {
        printf("vkQueuePresentKHR failed\n");
        std::exit(0);
    }
}

void keyboardEvent(SDL_Event* event) {
    using Emulator::HLE::Libraries::LibPad::ScePadButton;

    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        u32 button = 0;
        switch (event->key.keysym.sym) {
            case SDLK_UP: button = ScePadButton::SCE_PAD_BUTTON_UP; break;
            case SDLK_DOWN: button = ScePadButton::SCE_PAD_BUTTON_DOWN; break;
            case SDLK_LEFT: button = ScePadButton::SCE_PAD_BUTTON_LEFT; break;
            case SDLK_RIGHT: button = ScePadButton::SCE_PAD_BUTTON_RIGHT; break;
            case SDLK_KP_8: button = ScePadButton ::SCE_PAD_BUTTON_TRIANGLE; break;
            case SDLK_KP_6: button = ScePadButton ::SCE_PAD_BUTTON_CIRCLE; break;
            case SDLK_KP_2: button = ScePadButton ::SCE_PAD_BUTTON_CROSS; break;
            case SDLK_KP_4: button = ScePadButton ::SCE_PAD_BUTTON_SQUARE; break;
            case SDLK_RETURN: button = ScePadButton ::SCE_PAD_BUTTON_OPTIONS; break;

            default: break;
        }
        if (button != 0) {
            auto* controller = Singleton<Emulator::Host::Controller::GameController>::Instance();
            controller->checKButton(0, button, event->type == SDL_EVENT_KEY_DOWN);
        }
    }
}

}  // namespace Emu