/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**************************************************************************************************
 *** This file was autogenerated from GrRRectBlurEffect.fp; do not modify.
 **************************************************************************************************/
#include "GrRRectBlurEffect.h"
#if SK_SUPPORT_GPU

std::unique_ptr<GrFragmentProcessor> GrRRectBlurEffect::Make(GrContext* context, float sigma,
                                                             float xformedSigma,
                                                             const SkRRect& srcRRect,
                                                             const SkRRect& devRRect) {
    SkASSERT(!SkRRectPriv::IsCircle(devRRect) &&
             !devRRect.isRect());  // Should've been caught up-stream

    // TODO: loosen this up
    if (!SkRRectPriv::IsSimpleCircular(devRRect)) {
        return nullptr;
    }

    // Make sure we can successfully ninepatch this rrect -- the blur sigma has to be
    // sufficiently small relative to both the size of the corner radius and the
    // width (and height) of the rrect.
    SkRRect rrectToDraw;
    SkISize size;
    SkScalar ignored[SkBlurMaskFilter::kMaxDivisions];
    int ignoredSize;
    uint32_t ignored32;

    bool ninePatchable = SkBlurMaskFilter::ComputeBlurredRRectParams(
            srcRRect, devRRect, SkRect::MakeEmpty(), sigma, xformedSigma, &rrectToDraw, &size,
            ignored, ignored, ignored, ignored, &ignoredSize, &ignoredSize, &ignored32);
    if (!ninePatchable) {
        return nullptr;
    }

    sk_sp<GrTextureProxy> mask(
            find_or_create_rrect_blur_mask(context, rrectToDraw, size, xformedSigma));
    if (!mask) {
        return nullptr;
    }

    return std::unique_ptr<GrFragmentProcessor>(
            new GrRRectBlurEffect(xformedSigma, devRRect.getBounds(),
                                  SkRRectPriv::GetSimpleRadii(devRRect).fX, std::move(mask)));
}
#include "glsl/GrGLSLFragmentProcessor.h"
#include "glsl/GrGLSLFragmentShaderBuilder.h"
#include "glsl/GrGLSLProgramBuilder.h"
#include "GrTexture.h"
#include "SkSLCPP.h"
#include "SkSLUtil.h"
class GrGLSLRRectBlurEffect : public GrGLSLFragmentProcessor {
public:
    GrGLSLRRectBlurEffect() {}
    void emitCode(EmitArgs& args) override {
        GrGLSLFPFragmentBuilder* fragBuilder = args.fFragBuilder;
        const GrRRectBlurEffect& _outer = args.fFp.cast<GrRRectBlurEffect>();
        (void)_outer;
        auto sigma = _outer.sigma();
        (void)sigma;
        auto rect = _outer.rect();
        (void)rect;
        auto cornerRadius = _outer.cornerRadius();
        (void)cornerRadius;
        fCornerRadiusVar = args.fUniformHandler->addUniform(kFragment_GrShaderFlag, kFloat_GrSLType,
                                                            kDefault_GrSLPrecision, "cornerRadius");
        fProxyRectVar = args.fUniformHandler->addUniform(kFragment_GrShaderFlag, kFloat4_GrSLType,
                                                         kDefault_GrSLPrecision, "proxyRect");
        fBlurRadiusVar = args.fUniformHandler->addUniform(kFragment_GrShaderFlag, kHalf_GrSLType,
                                                          kDefault_GrSLPrecision, "blurRadius");
        fragBuilder->codeAppendf(
                "\nhalf2 translatedFragPos = half2(sk_FragCoord.xy - %s.xy);\nhalf threshold = "
                "half(%s + 2.0 * float(%s));\nhalf2 middle = half2((%s.zw - %s.xy) - 2.0 * "
                "float(threshold));\nif (translatedFragPos.x >= threshold && translatedFragPos.x < "
                "middle.x + threshold) {\n    translatedFragPos.x = threshold;\n} else if "
                "(translatedFragPos.x >= middle.x + threshold) {\n    translatedFragPos.x -= "
                "float(middle.x) - 1.0;\n}\nif (translatedFragPos.y > threshold && "
                "translatedFragPos.y < middle.y + threshold) {\n    translatedFr",
                args.fUniformHandler->getUniformCStr(fProxyRectVar),
                args.fUniformHandler->getUniformCStr(fCornerRadiusVar),
                args.fUniformHandler->getUniformCStr(fBlurRadiusVar),
                args.fUniformHandler->getUniformCStr(fProxyRectVar),
                args.fUniformHandler->getUniformCStr(fProxyRectVar));
        fragBuilder->codeAppendf(
                "agPos.y = threshold;\n} else if (translatedFragPos.y >= middle.y + threshold) {\n "
                "   translatedFragPos.y -= float(middle.y) - 1.0;\n}\nhalf2 proxyDims = "
                "half2(half(2.0 * float(threshold) + 1.0));\nhalf2 texCoord = translatedFragPos / "
                "proxyDims;\n%s = %s * texture(%s, float2(texCoord)).%s;\n",
                args.fOutputColor, args.fInputColor ? args.fInputColor : "half4(1)",
                fragBuilder->getProgramBuilder()->samplerVariable(args.fTexSamplers[0]).c_str(),
                fragBuilder->getProgramBuilder()->samplerSwizzle(args.fTexSamplers[0]).c_str());
    }

private:
    void onSetData(const GrGLSLProgramDataManager& pdman,
                   const GrFragmentProcessor& _proc) override {
        const GrRRectBlurEffect& _outer = _proc.cast<GrRRectBlurEffect>();
        { pdman.set1f(fCornerRadiusVar, _outer.cornerRadius()); }
        auto sigma = _outer.sigma();
        (void)sigma;
        auto rect = _outer.rect();
        (void)rect;
        UniformHandle& cornerRadius = fCornerRadiusVar;
        (void)cornerRadius;
        GrSurfaceProxy& ninePatchSamplerProxy = *_outer.textureSampler(0).proxy();
        GrTexture& ninePatchSampler = *ninePatchSamplerProxy.priv().peekTexture();
        (void)ninePatchSampler;
        UniformHandle& proxyRect = fProxyRectVar;
        (void)proxyRect;
        UniformHandle& blurRadius = fBlurRadiusVar;
        (void)blurRadius;

        float blurRadiusValue = 3.f * SkScalarCeilToScalar(sigma - 1 / 6.0f);
        pdman.set1f(blurRadius, blurRadiusValue);

        SkRect outset = rect;
        outset.outset(blurRadiusValue, blurRadiusValue);
        pdman.set4f(proxyRect, outset.fLeft, outset.fTop, outset.fRight, outset.fBottom);
    }
    UniformHandle fProxyRectVar;
    UniformHandle fBlurRadiusVar;
    UniformHandle fCornerRadiusVar;
};
GrGLSLFragmentProcessor* GrRRectBlurEffect::onCreateGLSLInstance() const {
    return new GrGLSLRRectBlurEffect();
}
void GrRRectBlurEffect::onGetGLSLProcessorKey(const GrShaderCaps& caps,
                                              GrProcessorKeyBuilder* b) const {}
bool GrRRectBlurEffect::onIsEqual(const GrFragmentProcessor& other) const {
    const GrRRectBlurEffect& that = other.cast<GrRRectBlurEffect>();
    (void)that;
    if (fSigma != that.fSigma) return false;
    if (fRect != that.fRect) return false;
    if (fCornerRadius != that.fCornerRadius) return false;
    if (fNinePatchSampler != that.fNinePatchSampler) return false;
    return true;
}
GrRRectBlurEffect::GrRRectBlurEffect(const GrRRectBlurEffect& src)
        : INHERITED(kGrRRectBlurEffect_ClassID, src.optimizationFlags())
        , fSigma(src.fSigma)
        , fRect(src.fRect)
        , fCornerRadius(src.fCornerRadius)
        , fNinePatchSampler(src.fNinePatchSampler) {
    this->addTextureSampler(&fNinePatchSampler);
}
std::unique_ptr<GrFragmentProcessor> GrRRectBlurEffect::clone() const {
    return std::unique_ptr<GrFragmentProcessor>(new GrRRectBlurEffect(*this));
}
GR_DEFINE_FRAGMENT_PROCESSOR_TEST(GrRRectBlurEffect);
#if GR_TEST_UTILS
std::unique_ptr<GrFragmentProcessor> GrRRectBlurEffect::TestCreate(GrProcessorTestData* d) {
    SkScalar w = d->fRandom->nextRangeScalar(100.f, 1000.f);
    SkScalar h = d->fRandom->nextRangeScalar(100.f, 1000.f);
    SkScalar r = d->fRandom->nextRangeF(1.f, 9.f);
    SkScalar sigma = d->fRandom->nextRangeF(1.f, 10.f);
    SkRRect rrect;
    rrect.setRectXY(SkRect::MakeWH(w, h), r, r);
    return GrRRectBlurEffect::Make(d->context(), sigma, sigma, rrect, rrect);
}
#endif
#endif
