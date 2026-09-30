// Engine-owned Metal viewport (plan M01). Renders a ViewportDraw list into a
// ring of IOSurface-backed BGRA8 textures that a UI toolkit composites
// without copying (elisa-ui: CALayer.contents = IOSurface). Works without a
// window, so it is validated headlessly by reading the IOSurface back.
//
// Protocol (mirrors src/viewport/viewport_policy.elisa):
//  - the engine owns RING (3) surfaces of one size and a generation that
//    changes on every reallocation;
//  - render(slot) completes on the GPU before it returns, then the Elisa side
//    publishes (generation, frame, slot);
//  - the host displays surfaces it looked up for the current generation and
//    releases them when the generation changes. Surfaces are CFRetained by
//    the host (CALayer retains its contents), so a reallocation never pulls
//    memory from under a layer still showing the previous frame.
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#include <stdint.h>
#include <string.h>

#define ELISA_VP_RING 3

typedef struct {
    float x, y, z;
    uint32_t rgba;
} ElisaVpVertex;

@interface ElisaViewportMetal : NSObject
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> queue;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipeline;
@property(nonatomic, strong) id<MTLDepthStencilState> depthOn;
@property(nonatomic, strong) id<MTLDepthStencilState> depthOff;
@property(nonatomic, strong) id<MTLTexture> depth;
@property(nonatomic, strong) id<MTLRenderPipelineState> backdropPipeline;
@property(nonatomic, strong) id<MTLTexture> backdrop;
@property(nonatomic) int32_t width;
@property(nonatomic) int32_t height;
@property(nonatomic) int64_t generation;
@property(nonatomic) int64_t frames;
@end

@implementation ElisaViewportMetal {
  @public
    IOSurfaceRef surfaces[ELISA_VP_RING];
    id<MTLTexture> textures[ELISA_VP_RING];
}
- (void)dealloc {
    for (int k = 0; k < ELISA_VP_RING; ++k) {
        textures[k] = nil;
        if (surfaces[k]) CFRelease(surfaces[k]);
        surfaces[k] = NULL;
    }
}
@end

static NSString* const kShader =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct VIn { packed_float3 p; uint rgba; };\n"
     "struct VOut { float4 pos [[position]]; float4 color; float size [[point_size]]; };\n"
     "vertex VOut vp_vertex(const device VIn* v [[buffer(0)]], constant float4x4& m [[buffer(1)]], uint id [[vertex_id]]) {\n"
     "  VOut o; o.pos = m * float4(float3(v[id].p), 1.0);\n"
     "  uint c = v[id].rgba;\n"
     "  o.color = float4(float(c & 255u), float((c >> 8) & 255u), float((c >> 16) & 255u), 255.0) / 255.0;\n"
     "  o.size = 1.0; return o; }\n"
     "fragment float4 vp_fragment(VOut i [[stage_in]]) { return i.color; }\n"
     "struct BOut { float4 pos [[position]]; float2 uv; };\n"
     "vertex BOut vp_backdrop_vertex(uint id [[vertex_id]]) {\n"
     "  float2 uv = float2((id << 1) & 2, id & 2); BOut o;\n"
     "  o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 1.0, 1.0); o.uv = uv; return o; }\n"
     "fragment float4 vp_backdrop_fragment(BOut i [[stage_in]], texture2d<float> t [[texture(0)]]) {\n"
     "  constexpr sampler s(filter::linear, address::clamp_to_edge);\n"
     "  return float4(t.sample(s, i.uv).rgb, 1.0); }\n";

static int elisa_vp_allocate(ElisaViewportMetal* vp, int32_t width, int32_t height) {
    for (int k = 0; k < ELISA_VP_RING; ++k) {
        vp->textures[k] = nil;
        if (vp->surfaces[k]) CFRelease(vp->surfaces[k]);
        vp->surfaces[k] = NULL;
    }
    NSDictionary* props = @{
        (id)kIOSurfaceWidth : @(width),
        (id)kIOSurfaceHeight : @(height),
        (id)kIOSurfaceBytesPerElement : @4,
        (id)kIOSurfacePixelFormat : @((uint32_t)'BGRA'),
    };
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                    width:(NSUInteger)width
                                                                                   height:(NSUInteger)height
                                                                                mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeShared;
    for (int k = 0; k < ELISA_VP_RING; ++k) {
        vp->surfaces[k] = IOSurfaceCreate((__bridge CFDictionaryRef)props);
        if (!vp->surfaces[k]) return 0;
        vp->textures[k] = [vp.device newTextureWithDescriptor:desc iosurface:vp->surfaces[k] plane:0];
        if (!vp->textures[k]) return 0;
    }
    MTLTextureDescriptor* ddesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                     width:(NSUInteger)width
                                                                                    height:(NSUInteger)height
                                                                                 mipmapped:NO];
    ddesc.usage = MTLTextureUsageRenderTarget;
    ddesc.storageMode = MTLStorageModePrivate;
    vp.depth = [vp.device newTextureWithDescriptor:ddesc];
    if (!vp.depth) return 0;
    vp.width = width;
    vp.height = height;
    vp.generation = vp.generation + 1;
    return 1;
}

void* elisa_viewport_metal_create(int32_t width, int32_t height) {
    @autoreleasepool {
        if (width < 1 || height < 1 || width > 16384 || height > 16384) return NULL;
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return NULL;
        ElisaViewportMetal* vp = [ElisaViewportMetal new];
        vp.device = device;
        vp.queue = [device newCommandQueue];
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:kShader options:nil error:&error];
        if (!library) return NULL;
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [library newFunctionWithName:@"vp_vertex"];
        pd.fragmentFunction = [library newFunctionWithName:@"vp_fragment"];
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        vp.pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&error];
        if (!vp.pipeline) return NULL;
        pd.vertexFunction = [library newFunctionWithName:@"vp_backdrop_vertex"];
        pd.fragmentFunction = [library newFunctionWithName:@"vp_backdrop_fragment"];
        vp.backdropPipeline = [device newRenderPipelineStateWithDescriptor:pd error:&error];
        if (!vp.backdropPipeline) return NULL;
        MTLDepthStencilDescriptor* on = [MTLDepthStencilDescriptor new];
        on.depthCompareFunction = MTLCompareFunctionLessEqual;
        on.depthWriteEnabled = YES;
        vp.depthOn = [device newDepthStencilStateWithDescriptor:on];
        MTLDepthStencilDescriptor* off = [MTLDepthStencilDescriptor new];
        off.depthCompareFunction = MTLCompareFunctionAlways;
        off.depthWriteEnabled = NO;
        vp.depthOff = [device newDepthStencilStateWithDescriptor:off];
        if (!elisa_vp_allocate(vp, width, height)) return NULL;
        return (__bridge_retained void*)vp;
    }
}

void elisa_viewport_metal_destroy(void* handle) {
    if (!handle) return;
    @autoreleasepool {
        ElisaViewportMetal* vp = (__bridge_transfer ElisaViewportMetal*)handle;
        (void)vp;
    }
}

// 1 on success; the generation changes only when the size did.
int32_t elisa_viewport_metal_resize(void* handle, int32_t width, int32_t height) {
    if (!handle || width < 1 || height < 1 || width > 16384 || height > 16384) return 0;
    @autoreleasepool {
        ElisaViewportMetal* vp = (__bridge ElisaViewportMetal*)handle;
        if (vp.width == width && vp.height == height) return 1;
        return elisa_vp_allocate(vp, width, height);
    }
}

int64_t elisa_viewport_metal_generation(void* handle) {
    if (!handle) return 0;
    return ((__bridge ElisaViewportMetal*)handle).generation;
}

int32_t elisa_viewport_metal_width(void* handle) {
    return handle ? ((__bridge ElisaViewportMetal*)handle).width : 0;
}

int32_t elisa_viewport_metal_height(void* handle) {
    return handle ? ((__bridge ElisaViewportMetal*)handle).height : 0;
}

// IOSurfaceRef of `slot` (borrowed; the host CFRetains what it keeps).
void* elisa_viewport_metal_surface(void* handle, int32_t slot) {
    if (!handle || slot < 0 || slot >= ELISA_VP_RING) return NULL;
    return (void*)((__bridge ElisaViewportMetal*)handle)->surfaces[slot];
}

// Global IOSurfaceID of `slot`, for a host in another process.
uint32_t elisa_viewport_metal_surface_id(void* handle, int32_t slot) {
    void* surface = elisa_viewport_metal_surface(handle, slot);
    return surface ? IOSurfaceGetID((IOSurfaceRef)surface) : 0;
}

// Set (or, with width 0, clear) an RGBA8 backdrop that every later render
// stretches over the whole target before the draw list, e.g. a frame the
// Wicked renderer drew of the skinned mesh. The pixels are copied, so the
// caller may reuse its buffer. Returns 1 on success.
int32_t elisa_viewport_metal_set_backdrop(void* handle, const uint8_t* rgba, int32_t width, int32_t height) {
    if (!handle) return 0;
    @autoreleasepool {
        ElisaViewportMetal* vp = (__bridge ElisaViewportMetal*)handle;
        if (width == 0 && height == 0) {
            vp.backdrop = nil;
            return 1;
        }
        if (!rgba || width < 1 || height < 1 || width > 16384 || height > 16384) return 0;
        id<MTLTexture> texture = vp.backdrop;
        if (!texture || (int32_t)texture.width != width || (int32_t)texture.height != height) {
            MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                            width:(NSUInteger)width
                                                                                           height:(NSUInteger)height
                                                                                        mipmapped:NO];
            desc.usage = MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModeShared;
            texture = [vp.device newTextureWithDescriptor:desc];
            if (!texture) return 0;
        }
        [texture replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)width, (NSUInteger)height)
                   mipmapLevel:0
                     withBytes:rgba
                   bytesPerRow:(NSUInteger)width * 4];
        vp.backdrop = texture;
        return 1;
    }
}

int32_t elisa_viewport_metal_clear_backdrop(void* handle) {
    return elisa_viewport_metal_set_backdrop(handle, NULL, 0, 0);
}

static void elisa_vp_draw(id<MTLRenderCommandEncoder> enc, id<MTLBuffer> buffer, NSUInteger first, NSUInteger count,
                          MTLPrimitiveType type) {
    if (count == 0) return;
    [enc setVertexBuffer:buffer offset:first * sizeof(ElisaVpVertex) atIndex:0];
    [enc drawPrimitives:type vertexStart:0 vertexCount:count];
}

// Render the four batches (depth tris, depth lines, top tris, top lines, in
// that order in `positions`/`colors`) into `slot`, clearing to `clear_rgba`.
// Blocks until the GPU finishes, so the slot is complete when this returns.
// Returns 1 on success, 0 on bad arguments or a GPU error.
int32_t elisa_viewport_metal_render(void* handle, int32_t slot, const float* matrix, const float* positions,
                                    const uint32_t* colors, int64_t depth_tris, int64_t depth_lines, int64_t top_tris,
                                    int64_t top_lines, uint32_t clear_rgba) {
    if (!handle || slot < 0 || slot >= ELISA_VP_RING || !matrix) return 0;
    if (depth_tris < 0 || depth_lines < 0 || top_tris < 0 || top_lines < 0) return 0;
    if (depth_tris % 3 || top_tris % 3 || depth_lines % 2 || top_lines % 2) return 0;
    int64_t total = depth_tris + depth_lines + top_tris + top_lines;
    if (total > 4194304) return 0;
    if (total > 0 && (!positions || !colors)) return 0;
    @autoreleasepool {
        ElisaViewportMetal* vp = (__bridge ElisaViewportMetal*)handle;
        id<MTLBuffer> buffer = nil;
        if (total > 0) {
            buffer = [vp.device newBufferWithLength:(NSUInteger)total * sizeof(ElisaVpVertex)
                                            options:MTLResourceStorageModeShared];
            ElisaVpVertex* out = (ElisaVpVertex*)buffer.contents;
            for (int64_t k = 0; k < total; ++k) {
                out[k].x = positions[k * 3];
                out[k].y = positions[k * 3 + 1];
                out[k].z = positions[k * 3 + 2];
                out[k].rgba = colors[k];
            }
        }
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = vp->textures[slot];
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor =
            MTLClearColorMake((clear_rgba & 255u) / 255.0, ((clear_rgba >> 8) & 255u) / 255.0,
                              ((clear_rgba >> 16) & 255u) / 255.0, 1.0);
        pass.depthAttachment.texture = vp.depth;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = 1.0;
        id<MTLCommandBuffer> cmd = [vp.queue commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
        if (vp.backdrop) {
            [enc setRenderPipelineState:vp.backdropPipeline];
            [enc setDepthStencilState:vp.depthOff];
            [enc setFragmentTexture:vp.backdrop atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
        [enc setRenderPipelineState:vp.pipeline];
        [enc setVertexBytes:matrix length:16 * sizeof(float) atIndex:1];
        NSUInteger at = 0;
        [enc setDepthStencilState:vp.depthOn];
        elisa_vp_draw(enc, buffer, at, (NSUInteger)depth_tris, MTLPrimitiveTypeTriangle);
        at += (NSUInteger)depth_tris;
        elisa_vp_draw(enc, buffer, at, (NSUInteger)depth_lines, MTLPrimitiveTypeLine);
        at += (NSUInteger)depth_lines;
        [enc setDepthStencilState:vp.depthOff];
        elisa_vp_draw(enc, buffer, at, (NSUInteger)top_tris, MTLPrimitiveTypeTriangle);
        at += (NSUInteger)top_tris;
        elisa_vp_draw(enc, buffer, at, (NSUInteger)top_lines, MTLPrimitiveTypeLine);
        [enc endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];
        if (cmd.status != MTLCommandBufferStatusCompleted) return 0;
        vp.frames = vp.frames + 1;
        return 1;
    }
}

// Copy `slot` out as tightly packed RGBA8 (the IOSurface is BGRA8). Returns
// the bytes written, or 0 when `capacity` is too small.
int64_t elisa_viewport_metal_read_rgba(void* handle, int32_t slot, uint8_t* out, int64_t capacity) {
    IOSurfaceRef surface = (IOSurfaceRef)elisa_viewport_metal_surface(handle, slot);
    if (!surface || !out) return 0;
    size_t width = IOSurfaceGetWidth(surface);
    size_t height = IOSurfaceGetHeight(surface);
    if (capacity < (int64_t)(width * height * 4)) return 0;
    IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL);
    const uint8_t* base = (const uint8_t*)IOSurfaceGetBaseAddress(surface);
    size_t stride = IOSurfaceGetBytesPerRow(surface);
    for (size_t y = 0; y < height; ++y) {
        const uint8_t* row = base + y * stride;
        uint8_t* dst = out + y * width * 4;
        for (size_t x = 0; x < width; ++x) {
            dst[x * 4] = row[x * 4 + 2];
            dst[x * 4 + 1] = row[x * 4 + 1];
            dst[x * 4 + 2] = row[x * 4];
            dst[x * 4 + 3] = row[x * 4 + 3];
        }
    }
    IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL);
    return (int64_t)(width * height * 4);
}
