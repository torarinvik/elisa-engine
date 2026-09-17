// Environment control for the Wicked black-frame hunt: raw Metal through
// the same hidden SDL window setup, with no engine code involved. Stage A
// clears a shared texture red and reads it back (submission + readback).
// Stage B draws a fullscreen blue triangle over it (rasterization and a
// runtime-compiled shader). Exit 0 only if both stages read back the
// expected colors; anything else isolates the failure to this file's
// setup rather than the engine.
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <SDL3/SDL.h>
#import <SDL3/SDL_metal.h>

#include <cstdio>

namespace {

bool check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "metal control failed: %s\n", message);
        return false;
    }
    return true;
}

// Returns false when any channel of any sampled pixel deviates from the
// expected 8-bit value by more than two LSB steps of rounding noise.
bool expectColor(const uint8_t* bytes, int width, int height, int rowPitch,
    uint8_t b, uint8_t g, uint8_t r, const char* stage) {
    int bad = 0;
    for (int y = 0; y < height; y += 7) {
        for (int x = 0; x < width; x += 7) {
            const uint8_t* pixel = bytes + y * rowPitch + x * 4;
            int db = pixel[0] >= b ? pixel[0] - b : b - pixel[0];
            int dg = pixel[1] >= g ? pixel[1] - g : g - pixel[1];
            int dr = pixel[2] >= r ? pixel[2] - r : r - pixel[2];
            if (db > 2 || dg > 2 || dr > 2) {
                ++bad;
            }
        }
    }
    std::fprintf(stdout, "%s sampled deviance=%d\n", stage, bad);
    return bad == 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 1) {
        std::fprintf(stderr, "usage: metal_triangle\n");
        return 2;
    }
    if (!check(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization")) {
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(
        "elisa-metal-control", 320, 200, SDL_WINDOW_HIDDEN | SDL_WINDOW_METAL);
    if (!check(window != nullptr, "hidden Metal window")) {
        SDL_Quit();
        return 1;
    }
    SDL_MetalView view = SDL_Metal_CreateView(window);
    if (!check(view != nullptr, "metal view")) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    CAMetalLayer* layer = (CAMetalLayer*)SDL_Metal_GetLayer(view);
    if (!check(layer != nil, "metal layer")) {
        SDL_Metal_DestroyView(view);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!check(device != nil, "metal device")) {
        return 1;
    }
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;
    layer.drawableSize = CGSizeMake(320, 200);

    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:320
                                    height:200
                                 mipmapped:NO];
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    id<MTLTexture> target = [device newTextureWithDescriptor:desc];
    if (!check(target != nil, "shared target")) {
        return 1;
    }
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!check(queue != nil, "command queue")) {
        return 1;
    }

    // Stage A: clear-only. No shaders, no geometry: pure submission.
    {
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(1.0, 0.0, 0.0, 1.0);
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        [encoder endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (!check(commands.status != MTLCommandBufferStatusError, "clear submit")) {
            return 1;
        }
        uint8_t bytes[320 * 200 * 4];
        [target getBytes:bytes
             bytesPerRow:320 * 4
            fromRegion:MTLRegionMake2D(0, 0, 320, 200)
           mipmapLevel:0];
        // BGRA bytes of an opaque red clear.
        if (!check(expectColor(bytes, 320, 200, 320 * 4, 0, 0, 255, "clear"), "clear pixels")) {
            return 1;
        }
    }
    std::fprintf(stdout, "stage A (clear) passed\n");

    // Stage B: a fullscreen blue triangle over a red clear. Covers every
    // pixel, so viewport, scissor, culling, and depth cannot hide it; only
    // rasterization and shader execution decide the color.
    NSString* source = @"#include <metal_stdlib>\n"
        @"using namespace metal;\n"
        @"vertex float4 tri_vertex(uint id [[vertex_id]]) {\n"
        @"  float2 corners[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};\n"
        @"  return float4(corners[id], 0, 1);\n"
        @"}\n"
        @"fragment float4 tri_fragment() { return float4(0.2, 0.7, 1.0, 1.0); }\n";
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!check(library != nil, [[error localizedDescription] UTF8String] ?: "shader compile")) {
        return 1;
    }
    id<MTLFunction> vertexFn = [library newFunctionWithName:@"tri_vertex"];
    id<MTLFunction> fragmentFn = [library newFunctionWithName:@"tri_fragment"];
    if (!check(vertexFn != nil && fragmentFn != nil, "shader functions")) {
        return 1;
    }
    MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDesc.vertexFunction = vertexFn;
    pipelineDesc.fragmentFunction = fragmentFn;
    pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipelineDesc.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
    if (!check(pipeline != nil, [[error localizedDescription] UTF8String] ?: "pipeline state")) {
        return 1;
    }
    {
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(1.0, 0.0, 0.0, 1.0);
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:pipeline];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (!check(commands.status != MTLCommandBufferStatusError, "draw submit")) {
            return 1;
        }
        uint8_t bytes[320 * 200 * 4];
        [target getBytes:bytes
             bytesPerRow:320 * 4
            fromRegion:MTLRegionMake2D(0, 0, 320, 200)
           mipmapLevel:0];
        // BGRA bytes of the (0.2, 0.7, 1.0) fragment color.
        if (!check(expectColor(bytes, 320, 200, 320 * 4, 255, 179, 51, "draw"), "draw pixels")) {
            return 1;
        }
    }
    std::fprintf(stdout, "stage B (triangle) passed\n");

    SDL_Metal_DestroyView(view);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::fprintf(stdout, "metal control passed\n");
    return 0;
}
