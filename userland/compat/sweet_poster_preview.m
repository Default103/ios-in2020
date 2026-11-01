/* Targeted CPU preview when PosterBoard cannot obtain a Metal command queue.
 * The exact 20H392 _imageForView: implementation requires a queue in its
 * CARenderer options dictionary. No global dictionary/exception hooks.
 */
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <math.h>
#include <stdio.h>

static UIImage *(*original_image)(id, SEL, UIView *);
static unsigned previews;

static UIImage *cpu_preview(id self, SEL selector, UIView *view)
{
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device && [device newCommandQueue]) return original_image(self, selector, view);
    if (![view isKindOfClass:UIView.class] || !NSThread.isMainThread) {
        fprintf(stderr, "sweet-compat: PosterBoard CPU preview: unsupported view/thread\n");
        return nil;
    }
    CGRect bounds = view.layer.bounds;
    CGFloat scale = view.layer.contentsScale;
    if (!isfinite(scale) || scale <= 0) scale = 1;
    double w = ceil(bounds.size.width * scale), h = ceil(bounds.size.height * scale);
    if (!isfinite(w) || !isfinite(h) || w < 1 || h < 1 || w > 8192 || h > 8192 ||
        w * h > 16777216) return nil;
    [view layoutIfNeeded];
    UIGraphicsImageRendererFormat *format = [UIGraphicsImageRendererFormat defaultFormat];
    format.scale = scale;
    format.opaque = NO;
    format.preferredRange = UIGraphicsImageRendererFormatRangeStandard;
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithSize:bounds.size format:format];
    UIImage *image = [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) {
        CGContextTranslateCTM(context.CGContext, -bounds.origin.x, -bounds.origin.y);
        [view.layer renderInContext:context.CGContext];
    }];
    /* Match the original imageWithCGImage: pixel-size/scale=1 convention. */
    UIImage *result = image.CGImage ? [UIImage imageWithCGImage:image.CGImage
        scale:1 orientation:UIImageOrientationUp] : nil;
    if (__atomic_fetch_add(&previews, 1, __ATOMIC_RELAXED) < 16)
        fprintf(stderr, "sweet-compat: PosterBoard CPU preview without Metal queue: %.0fx%.0f %s\n",
            w, h, result ? "created" : "failed");
    return result;
}

void sweet_install_poster_preview(void)
{
    Class cls = objc_getClass("PBFHomeScreenPreviewImageBuilder");
    Method method = cls ? class_getInstanceMethod(cls, sel_registerName("_imageForView:")) : NULL;
    char result[8] = {0}, argument[8] = {0};
    if (method) {
        method_getReturnType(method, result, sizeof(result));
        method_getArgumentType(method, 2, argument, sizeof(argument));
    }
    if (!method || method_getNumberOfArguments(method) != 3 || result[0] != '@' || argument[0] != '@') {
        fprintf(stderr, "sweet-compat: PosterBoard preview method mismatch; no CPU hook\n");
        return;
    }
    original_image = (UIImage *(*)(id, SEL, UIView *))method_getImplementation(method);
    method_setImplementation(method, (IMP)cpu_preview);
    fprintf(stderr, "sweet-compat: PosterBoard missing-Metal-queue CPU preview hook installed\n");
}
