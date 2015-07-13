/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "SampleApp.h"

#include "SkData.h"
#include "SkCanvas.h"
#include "SkDevice.h"
#include "SkGraphics.h"
#include "SkImageDecoder.h"
#include "SkImageEncoder.h"
#include "SkPaint.h"
#include "SkPicture.h"
#include "SkStream.h"
#include "SkTSort.h"
#include "SkTime.h"
#include "SkWindow.h"

#include "SampleCode.h"
#include "SkTypeface.h"

#if SK_SUPPORT_GPU
#include "gl/GrGLUtil.h"
#include "gl/glew.h"
#include "GrRenderTarget.h"
#include "GrContext.h"
#include "SkGpuDevice.h"
#else
class GrContext;
#endif

#include "SkOSFile.h"
#include "SkStream.h"

#include "SkGPipe.h"
#include "SamplePipeControllers.h"
#include "OverView.h"
#include "TransitionView.h"

//extern SampleView* CreateSamplePictFileView(const char filename[]);
//
//class PictFileFactory : public SkViewFactory 
//{
//    SkString fFilename;
//public:
//    PictFileFactory(const SkString& filename) : fFilename(filename) {}
//    virtual SkView* operator() () const SK_OVERRIDE {
//        return CreateSamplePictFileView(fFilename.c_str());
//    }
//};

#define PIPE_FILEx
#ifdef  PIPE_FILE
#define FILE_PATH "/path/to/drawing.data"
#endif

#define PIPE_NETx
#ifdef  PIPE_NET
#include "SkSockets.h"
SkTCPServer gServer;
#endif

#define USE_ARROWS_FOR_ZOOM true

#define ANIMATING_EVENTTYPE "nextSample"
#define ANIMATING_DELAY     250

#ifdef SK_DEBUG
    #define FPS_REPEAT_MULTIPLIER   1
#else
    #define FPS_REPEAT_MULTIPLIER   10
#endif
#define FPS_REPEAT_COUNT    (10 * FPS_REPEAT_MULTIPLIER)

static SampleWindow* gSampleWindow;

static bool gShowGMBounds;

static void post_event_to_sink(SkEvent* evt, SkEventSink* sink) {
    evt->setTargetID(sink->getSinkID())->post();
}

///////////////////////////////////////////////////////////////////////////////

static const char* skip_until(const char* str, const char* skip) {
    if (!str) {
        return NULL;
    }
    return strstr(str, skip);
}

static const char* skip_past(const char* str, const char* skip) {
    const char* found = skip_until(str, skip);
    if (!found) {
        return NULL;
    }
    return found + strlen(skip);
}

static const char* gPrefFileName = "sampleapp_prefs.txt";

static bool readTitleFromPrefs(SkString* title) {
    SkFILEStream stream(gPrefFileName);
    if (!stream.isValid()) {
        return false;
    }

    int len = stream.getLength();
    SkString data(len);
    stream.read(data.writable_str(), len);
    const char* s = data.c_str();

    s = skip_past(s, "curr-slide-title");
    s = skip_past(s, "=");
    s = skip_past(s, "\"");
    const char* stop = skip_until(s, "\"");
    if (stop > s) {
        title->set(s, stop - s);
        return true;
    }
    return false;
}

static void writeTitleToPrefs(const char* title) {
    SkFILEWStream stream(gPrefFileName);
    SkString data;
    data.printf("curr-slide-title = \"%s\"\n", title);
    stream.write(data.c_str(), data.size());
}

///////////////////////////////////////////////////////////////////////////////

///////////////
static const char view_inval_msg[] = "view-inval-msg";

void SampleWindow::postInvalDelay() {
    (new SkEvent(view_inval_msg, this->getSinkID()))->postDelay(1);
}

static bool isInvalEvent(const SkEvent& evt) {
    return evt.isType(view_inval_msg);
}
//////////////////

SkFuncViewFactory::SkFuncViewFactory(SkViewCreateFunc func)
    : fCreateFunc(func) {
}

SkView* SkFuncViewFactory::operator() () const {
    return (*fCreateFunc)();
}

#include "GMSampleView.h"

SkGMSampleViewFactory::SkGMSampleViewFactory(GMFactoryFunc func)
    : fFunc(func) {
}

SkView* SkGMSampleViewFactory::operator() () const {
    return new GMSampleView(fFunc(NULL));
}

SkViewRegister* SkViewRegister::gHead;
SkViewRegister::SkViewRegister(SkViewFactory* fact) : fFact(fact) {
    fFact->ref();
    fChain = gHead;
    gHead = this;
}

SkViewRegister::SkViewRegister(SkViewCreateFunc func) {
    fFact = new SkFuncViewFactory(func);
    fChain = gHead;
    gHead = this;
}

SkViewRegister::SkViewRegister(GMFactoryFunc func) {
    fFact = new SkGMSampleViewFactory(func);
    fChain = gHead;
    gHead = this;
}

class AutoUnrefArray {
public:
    AutoUnrefArray() {}
    ~AutoUnrefArray() {
        int count = fObjs.count();
        for (int i = 0; i < count; ++i) {
            fObjs[i]->unref();
        }
    }
    SkRefCnt*& push_back() { return *fObjs.append(); }

private:
    SkTDArray<SkRefCnt*> fObjs;
};

// registers GMs as Samples
// This can't be performed during static initialization because it could be
// run before GMRegistry has been fully built.
static void SkGMRegistyToSampleRegistry() {
    static bool gOnce;
    static AutoUnrefArray fRegisters;

    if (!gOnce) {
        const skiagm::GMRegistry* gmreg = skiagm::GMRegistry::Head();
        while (gmreg) {
            fRegisters.push_back() = new SkViewRegister(gmreg->factory());
            gmreg = gmreg->next();
        }
        gOnce = true;
    }
}

//////////////////////////////////////////////////////////////////////////////

enum FlipAxisEnum {
    kFlipAxis_X = (1 << 0),
    kFlipAxis_Y = (1 << 1)
};

#include "SkDrawFilter.h"

struct HintingState {
    SkPaint::Hinting hinting;
    const char* name;
    const char* label;
};
static HintingState gHintingStates[] = {
    {SkPaint::kNo_Hinting, "Mixed", NULL },
    {SkPaint::kNo_Hinting, "None", "H0 " },
    {SkPaint::kSlight_Hinting, "Slight", "Hs " },
    {SkPaint::kNormal_Hinting, "Normal", "Hn " },
    {SkPaint::kFull_Hinting, "Full", "Hf " },
};

struct FilterLevelState {
    SkPaint::FilterLevel    fLevel;
    const char*             fName;
    const char*             fLabel;
};
static FilterLevelState gFilterLevelStates[] = {
    { SkPaint::kNone_FilterLevel,   "Mixed",    NULL    },
    { SkPaint::kNone_FilterLevel,   "None",     "F0 "   },
    { SkPaint::kLow_FilterLevel,    "Low",      "F1 "   },
    { SkPaint::kMedium_FilterLevel, "Medium",   "F2 "   },
    { SkPaint::kHigh_FilterLevel,   "High",     "F3 "   },
};

class FlagsDrawFilter : public SkDrawFilter {
public:
    FlagsDrawFilter(SkOSMenu::TriState lcd, SkOSMenu::TriState aa,
                    SkOSMenu::TriState subpixel, int hinting, int filterlevel)
        : fLCDState(lcd)
        , fAAState(aa)
        , fSubpixelState(subpixel)
        , fHintingState(hinting)
        , fFilterLevelIndex(filterlevel)
    {
    }

    virtual bool filter(SkPaint* paint, Type t) {
        if (kText_Type == t && SkOSMenu::kMixedState != fLCDState) {
            paint->setLCDRenderText(SkOSMenu::kOnState == fLCDState);
        }
        if (SkOSMenu::kMixedState != fAAState) {
            paint->setAntiAlias(SkOSMenu::kOnState == fAAState);
        }
        if (0 != fFilterLevelIndex) {
            paint->setFilterLevel(gFilterLevelStates[fFilterLevelIndex].fLevel);
        }
        if (SkOSMenu::kMixedState != fSubpixelState) {
            paint->setSubpixelText(SkOSMenu::kOnState == fSubpixelState);
        }
        if (0 != fHintingState && fHintingState < (int)SK_ARRAY_COUNT(gHintingStates)) {
            paint->setHinting(gHintingStates[fHintingState].hinting);
        }
        return true;
    }

private:
    SkOSMenu::TriState  fLCDState;
    SkOSMenu::TriState  fAAState;
    SkOSMenu::TriState  fSubpixelState;
    int fHintingState;
    int fFilterLevelIndex;
};

//////////////////////////////////////////////////////////////////////////////

#define MAX_ZOOM_LEVEL  8
#define MIN_ZOOM_LEVEL  -8

static const char gCharEvtName[] = "SampleCode_Char_Event";
static const char gKeyEvtName[] = "SampleCode_Key_Event";
static const char gTitleEvtName[] = "SampleCode_Title_Event";
static const char gPrefSizeEvtName[] = "SampleCode_PrefSize_Event";
static const char gFastTextEvtName[] = "SampleCode_FastText_Event";
static const char gUpdateWindowTitleEvtName[] = "SampleCode_UpdateWindowTitle";

bool SampleCode::CharQ(const SkEvent& evt, SkUnichar* outUni) {
    if (evt.isType(gCharEvtName, sizeof(gCharEvtName) - 1)) {
        if (outUni) {
            *outUni = evt.getFast32();
        }
        return true;
    }
    return false;
}

bool SampleCode::KeyQ(const SkEvent& evt, SkKey* outKey) {
    if (evt.isType(gKeyEvtName, sizeof(gKeyEvtName) - 1)) {
        if (outKey) {
            *outKey = (SkKey)evt.getFast32();
        }
        return true;
    }
    return false;
}

bool SampleCode::TitleQ(const SkEvent& evt) {
    return evt.isType(gTitleEvtName, sizeof(gTitleEvtName) - 1);
}

void SampleCode::TitleR(SkEvent* evt, const char title[]) {
    evt->setString(gTitleEvtName, title);
}

bool SampleCode::RequestTitle(SkView* view, SkString* title) {
    SkEvent evt(gTitleEvtName);
    if (view->doQuery(&evt)) {
        title->set(evt.findString(gTitleEvtName));
        return true;
    }
    return false;
}

bool SampleCode::PrefSizeQ(const SkEvent& evt) {
    return evt.isType(gPrefSizeEvtName, sizeof(gPrefSizeEvtName) - 1);
}

void SampleCode::PrefSizeR(SkEvent* evt, float width, float height) {
    float size[2];
    size[0] = width;
    size[1] = height;
    evt->setScalars(gPrefSizeEvtName, 2, size);
}

bool SampleCode::FastTextQ(const SkEvent& evt) {
    return evt.isType(gFastTextEvtName, sizeof(gFastTextEvtName) - 1);
}

///////////////////////////////////////////////////////////////////////////////

static SkMSec gAnimTime;
static SkMSec gAnimTimePrev;

SkMSec SampleCode::GetAnimTime() { return gAnimTime; }
SkMSec SampleCode::GetAnimTimeDelta() { return gAnimTime - gAnimTimePrev; }
float SampleCode::GetAnimSecondsDelta() {
    return SkDoubleToScalar(GetAnimTimeDelta() / 1000.0);
}

float SampleCode::GetAnimScalar(float speed, float period) {
    // since gAnimTime can be up to 32 bits, we can't convert it to a float
    // or we'll lose the low bits. Hence we use doubles for the intermediate
    // calculations
    double seconds = (double)gAnimTime / 1000.0;
    double value = SkScalarToDouble(speed) * seconds;
    if (period) {
        value = ::fmod(value, SkScalarToDouble(period));
    }
    return SkDoubleToScalar(value);
}

float SampleCode::GetAnimSinScalar(float amplitude,
                                      float periodInSec,
                                      float phaseInSec) {
    if (!periodInSec) {
        return 0;
    }
    double t = (double)gAnimTime / 1000.0 + phaseInSec;
    t *= SkScalarToFloat(2 * SK_ScalarPI) / periodInSec;
    amplitude = SK_ScalarHalf * amplitude;
    return SkScalarMul(amplitude, SkDoubleToScalar(sin(t))) + amplitude;
}

GrContext* SampleCode::GetGr() {
    return gSampleWindow ? gSampleWindow->getGrContext() : NULL;
}

// some GMs rely on having a skiagm::GetGr function defined
namespace skiagm {
    // FIXME: this should be moved into a header
    GrContext* GetGr();
    GrContext* GetGr() { return SampleCode::GetGr(); }
}

//////////////////////////////////////////////////////////////////////////////

enum TilingMode {
    kNo_Tiling,
    kAbs_128x128_Tiling,
    kAbs_256x256_Tiling,
    kRel_4x4_Tiling,
    kRel_1x16_Tiling,
    kRel_16x1_Tiling,

    kLast_TilingMode_Enum
};

struct TilingInfo {
    const char* label;
    float    w, h;
};

static const struct TilingInfo gTilingInfo[] = {
    { "No tiling", SK_Scalar1        , SK_Scalar1         }, // kNo_Tiling
    { "128x128"  , SkIntToScalar(128), SkIntToScalar(128) }, // kAbs_128x128_Tiling
    { "256x256"  , SkIntToScalar(256), SkIntToScalar(256) }, // kAbs_256x256_Tiling
    { "1/4x1/4"  , SK_Scalar1 / 4    , SK_Scalar1 / 4     }, // kRel_4x4_Tiling
    { "1/1x1/16" , SK_Scalar1        , SK_Scalar1 / 16    }, // kRel_1x16_Tiling
    { "1/16x1/1" , SK_Scalar1 / 16   , SK_Scalar1         }, // kRel_16x1_Tiling
};
SK_COMPILE_ASSERT((SK_ARRAY_COUNT(gTilingInfo) == kLast_TilingMode_Enum),
                  Incomplete_tiling_labels);

SkSize SampleWindow::tileSize() const {
    const struct TilingInfo* info = gTilingInfo + fTilingMode;
    return SkSize::Make(info->w > SK_Scalar1 ? info->w : this->width() * info->w,
                        info->h > SK_Scalar1 ? info->h : this->height() * info->h);
}
//////////////////////////////////////////////////////////////////////////////

static SkView* curr_view(SkWindow* wind) {
    SkView::F2BIter iter(wind);
    return iter.next();
}

static bool curr_title(SkWindow* wind, SkString* title) {
	SkView* view = curr_view(wind);
    if (view) {
        SkEvent evt(gTitleEvtName);
        if (view->doQuery(&evt)) {
            title->set(evt.findString(gTitleEvtName));
            return true;
        }
    }
    return false;
}

void SampleWindow::setZoomCenter(float x, float y)
{
    fZoomCenterX = x;
    fZoomCenterY = y;
}

bool SampleWindow::zoomIn()
{
    // Arbitrarily decided
    if (fFatBitsScale == 25) return false;
    fFatBitsScale++;
    this->inval(NULL);
    return true;
}

bool SampleWindow::zoomOut()
{
    if (fFatBitsScale == 1) return false;
    fFatBitsScale--;
    this->inval(NULL);
    return true;
}

void SampleWindow::updatePointer(int x, int y)
{
    fMouseX = x;
    fMouseY = y;
    if (fShowZoomer) {
        this->inval(NULL);
    }
}

static void usage(const char * argv0)
{
}

static SkString getSampleTitle(const SkViewFactory* sampleFactory) {
    SkView* view = (*sampleFactory)();
    SkString title;
    SampleCode::RequestTitle(view, &title);
    view->unref();
    return title;
}

static bool compareSampleTitle(const SkViewFactory* first, const SkViewFactory* second) {
    return strcmp(getSampleTitle(first).c_str(), getSampleTitle(second).c_str()) < 0;
}

SampleWindow::DeviceManager::DeviceManager()
{
	fCurContext = NULL;
	fCurRenderTarget = NULL;
	fMSAASampleCount = 0;
}

SampleWindow::DeviceManager::~DeviceManager() 
{
#if SK_SUPPORT_GPU
	SkSafeUnref(fCurContext);
	SkSafeUnref(fCurRenderTarget);
#endif
}

void SampleWindow::DeviceManager::setUpBackend(SampleWindow* win, int msaaSampleCount)
{
	AttachmentInfo attachmentInfo;
	bool result = win->attach( msaaSampleCount, &attachmentInfo);
	if (!result) {
		return;
	}
	fMSAASampleCount = msaaSampleCount;

	fCurContext = GrContext::Create();

	if (NULL == fCurContext)
	{
		// We need some context and interface to see results
		SkSafeUnref(fCurContext);
		win->detach();
	}
	// call windowSizeChanged to create the render target
	this->windowSizeChanged(win);
}

void SampleWindow::DeviceManager::tearDownBackend(SampleWindow *win) 
{
	SkSafeUnref(fCurContext);
	fCurContext = NULL;
	SkSafeUnref(fCurRenderTarget);
	fCurRenderTarget = NULL;

	win->detach();
}

SkCanvas* SampleWindow::DeviceManager::createCanvas()
{
	SkAutoTUnref<SkBaseDevice> device(new SkGpuDevice(fCurContext, fCurRenderTarget));
	//SkAutoTUnref<SkBaseDevice> device( SkGpuDevice::Create(fCurRenderTarget));
	return new SkCanvas(device);
}

void SampleWindow::DeviceManager::publishCanvas(
	SkCanvas* canvas,
	SampleWindow* win)
{
	if (fCurContext)
	{
		// in case we have queued drawing calls
		fCurContext->flush();
	}
	win->present();
}

void SampleWindow::DeviceManager::windowSizeChanged(SampleWindow* win) {
	if (fCurContext) {
		AttachmentInfo attachmentInfo;
		win->attach( fMSAASampleCount, &attachmentInfo);

		GrBackendRenderTargetDesc desc;
		desc.fWidth = SkScalarRoundToInt(win->width());
		desc.fHeight = SkScalarRoundToInt(win->height());
		desc.fConfig = kSkia8888_GrPixelConfig;
		desc.fOrigin = kBottomLeft_GrSurfaceOrigin;
		desc.fSampleCnt = attachmentInfo.fSampleCount;
		desc.fStencilBits = attachmentInfo.fStencilBits;
		GLint buffer;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &buffer);
		desc.fRenderTargetHandle = buffer;

		SkSafeUnref(fCurRenderTarget);
		fCurRenderTarget = fCurContext->wrapBackendRenderTarget(desc);
	}
}

GrContext* SampleWindow::DeviceManager::getGrContext()
{
	return fCurContext;
}

GrRenderTarget* SampleWindow::DeviceManager::getGrRenderTarget()
{
	return fCurRenderTarget;
}


SampleWindow::SampleWindow(void* hwnd, int argc, char** argv, DeviceManager* devManager)
    : SkOSWindow(hwnd)
    , fDevManager(NULL) {

    fCurrIndex = -1;

    this->registerPictFileSamples(argv, argc);
    this->registerPictFileSample(argv, argc);
#ifdef SAMPLE_PDF_FILE_VIEWER
    this->registerPdfFileViewerSamples(argv, argc);
#endif  // SAMPLE_PDF_FILE_VIEWER
    SkGMRegistyToSampleRegistry();
    {
        const SkViewRegister* reg = SkViewRegister::Head();
        while (reg) {
            *fSamples.append() = reg->factory();
            reg = reg->next();
        }
    }

    bool sort = false;
    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "--sort")) {
            sort = true;
            break;
        }
    }

    if (sort) {
        // Sort samples, so foo.skp and foo.pdf are consecutive and we can quickly spot where
        // skp -> pdf -> png fails.
        SkTQSort(fSamples.begin(), fSamples.end() ? fSamples.end() - 1 : NULL, compareSampleTitle);
    }

    const char* resourcePath = NULL;
    fMSAASampleCount = 0;

    const char* const commandName = argv[0];
    char* const* stop = argv + argc;
    for (++argv; argv < stop; ++argv) {
        if (!strcmp(*argv, "-i") || !strcmp(*argv, "--resourcePath")) {
            argv++;
            if (argv < stop && **argv) {
                resourcePath = *argv;
            }
        } else if (strcmp(*argv, "--slide") == 0) {
            argv++;
            if (argv < stop && **argv) {
                fCurrIndex = findByTitle(*argv);
                if (fCurrIndex < 0) {
                    fprintf(stderr, "Unknown sample \"%s\"\n", *argv);
                    listTitles();
                }
            }
        } else if (strcmp(*argv, "--msaa") == 0) {
            ++argv;
            if (argv < stop && **argv) {
                fMSAASampleCount = atoi(*argv);
            }
        } else if (strcmp(*argv, "--list") == 0) {
            listTitles();
        } else if (strcmp(*argv, "--pictureDir") == 0) {
            ++argv;  // This case is dealt with in registerPictFileSamples().
        } else if (strcmp(*argv, "--picture") == 0) {
            ++argv;  // This case is dealt with in registerPictFileSample().
        }
        else {
            usage(commandName);
        }
    }

    if (fCurrIndex < 0) {
        SkString title;
        if (readTitleFromPrefs(&title)) {
            fCurrIndex = findByTitle(title.c_str());
        }
    }

    if (fCurrIndex < 0) {
       fCurrIndex = 0;
    }

	fCurrIndex = fSamples.count() - 1;

    gSampleWindow = this;

#ifdef  PIPE_FILE
    //Clear existing file or create file if it doesn't exist
    FILE* f = fopen(FILE_PATH, "wb");
    fclose(f);
#endif

    fPicture = NULL;

    fUseClip = false;
    fNClip = false;
    fAnimating = false;
    fRotate = false;
    fRotateAnimTime = 0;
    fPerspAnim = false;
    fPerspAnimTime = 0;
    fRequestGrabImage = false;
    fPipeState = SkOSMenu::kOffState;
    fTilingMode = kNo_Tiling;
    fMeasureFPS = false;
    fLCDState = SkOSMenu::kMixedState;
    fAAState = SkOSMenu::kMixedState;
    fSubpixelState = SkOSMenu::kMixedState;
    fHintingState = 0;
    fFilterLevelIndex = 0;
    fFlipAxis = 0;
    fScrollTestX = fScrollTestY = 0;

    fMouseX = fMouseY = 0;
    fFatBitsScale = 8;
    fTypeface = SkTypeface::CreateFromTypeface(NULL, SkTypeface::kBold);
    fShowZoomer = false;

    fZoomLevel = 0;
    fZoomScale = SK_Scalar1;

    fMagnify = false;

    fSaveToPdf = false;
    fPdfCanvas = NULL;

    fTransitionNext = 6;
    fTransitionPrev = 2;

    int sinkID = this->getSinkID();
    fAppMenu = new SkOSMenu;
    fAppMenu->setTitle("Global Settings");
    int itemID;

    itemID =fAppMenu->appendList("Device Type", "Device Type", sinkID, 0,
                                "Raster", "Picture", "OpenGL",
                                NULL);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'd');
    itemID = fAppMenu->appendTriState("AA", "AA", sinkID, fAAState);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'b');
    itemID = fAppMenu->appendTriState("LCD", "LCD", sinkID, fLCDState);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'l');
    itemID = fAppMenu->appendList("FilterLevel", "FilterLevel", sinkID, fFilterLevelIndex,
                                  gFilterLevelStates[0].fName,
                                  gFilterLevelStates[1].fName,
                                  gFilterLevelStates[2].fName,
                                  gFilterLevelStates[3].fName,
                                  gFilterLevelStates[4].fName,
                                  NULL);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'n');
    itemID = fAppMenu->appendTriState("Subpixel", "Subpixel", sinkID, fSubpixelState);
    fAppMenu->assignKeyEquivalentToItem(itemID, 's');
    itemID = fAppMenu->appendList("Hinting", "Hinting", sinkID, fHintingState,
                                  gHintingStates[0].name,
                                  gHintingStates[1].name,
                                  gHintingStates[2].name,
                                  gHintingStates[3].name,
                                  gHintingStates[4].name,
                                  NULL);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'h');

    fUsePipeMenuItemID = fAppMenu->appendTriState("Pipe", "Pipe" , sinkID,
                                                  fPipeState);
    fAppMenu->assignKeyEquivalentToItem(fUsePipeMenuItemID, 'P');

    itemID =fAppMenu->appendList("Tiling", "Tiling", sinkID, fTilingMode,
                                 gTilingInfo[kNo_Tiling].label,
                                 gTilingInfo[kAbs_128x128_Tiling].label,
                                 gTilingInfo[kAbs_256x256_Tiling].label,
                                 gTilingInfo[kRel_4x4_Tiling].label,
                                 gTilingInfo[kRel_1x16_Tiling].label,
                                 gTilingInfo[kRel_16x1_Tiling].label,
                                 NULL);
    fAppMenu->assignKeyEquivalentToItem(itemID, 't');

    itemID = fAppMenu->appendSwitch("Slide Show", "Slide Show" , sinkID, false);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'a');
    itemID = fAppMenu->appendSwitch("Clip", "Clip" , sinkID, fUseClip);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'c');
    itemID = fAppMenu->appendSwitch("Flip X", "Flip X" , sinkID, false);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'x');
    itemID = fAppMenu->appendSwitch("Flip Y", "Flip Y" , sinkID, false);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'y');
    itemID = fAppMenu->appendSwitch("Zoomer", "Zoomer" , sinkID, fShowZoomer);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'z');
    itemID = fAppMenu->appendSwitch("Magnify", "Magnify" , sinkID, fMagnify);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'm');
    itemID =fAppMenu->appendList("Transition-Next", "Transition-Next", sinkID,
                                fTransitionNext, "Up", "Up and Right", "Right",
                                "Down and Right", "Down", "Down and Left",
                                "Left", "Up and Left", NULL);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'j');
    itemID =fAppMenu->appendList("Transition-Prev", "Transition-Prev", sinkID,
                                fTransitionPrev, "Up", "Up and Right", "Right",
                                "Down and Right", "Down", "Down and Left",
                                "Left", "Up and Left", NULL);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'k');
    itemID = fAppMenu->appendAction("Save to PDF", sinkID);
    fAppMenu->assignKeyEquivalentToItem(itemID, 'e');

    this->addMenu(fAppMenu);
    fSlideMenu = new SkOSMenu;
    this->addMenu(fSlideMenu);

//    this->setConfig(SkBitmap::kRGB_565_Config);
    this->setConfig(SkBitmap::kARGB_8888_Config);
    this->setVisibleP(true);
    this->setClipToBounds(false);

    skiagm::GM::SetResourcePath(resourcePath);
	
    this->loadView((*fSamples[fCurrIndex])());

    fPDFData = NULL;

    if (NULL == devManager) 
	{
        fDevManager = new DeviceManager();
    } 
	else 
	{
        devManager->ref();
        fDevManager = devManager;
    }
    fDevManager->setUpBackend(this, fMSAASampleCount);

    // If another constructor set our dimensions, ensure that our
    // onSizeChange gets called.
    if (this->height() && this->width()) {
        this->onSizeChange();
    }

    // can't call this synchronously, since it may require a subclass to
    // to implement, or the caller may need us to have returned from the
    // constructor first. Hence we post an event to ourselves.
//    this->updateTitle();
    post_event_to_sink(new SkEvent(gUpdateWindowTitleEvtName), this);
}

SampleWindow::~SampleWindow() {
    delete fPicture;
    delete fPdfCanvas;
    fTypeface->unref();

    SkSafeUnref(fDevManager);
}

SkCanvas* SampleWindow::createCanvas()
{
	SkCanvas* canvas = NULL;
	if (fDevManager) {
		canvas = fDevManager->createCanvas();
	}
	if (NULL == canvas) {
		canvas = this->SkOSWindow::createCanvas();
	}
	return canvas;
}

static void make_filepath(SkString* path, const char* dir, const SkString& name) {
    size_t len = strlen(dir);
    path->set(dir);
    if (len > 0 && dir[len - 1] != '/') {
        path->append("/");
    }
    path->append(name);
}

void SampleWindow::registerPictFileSample(char** argv, int argc) {
    const char* pict = NULL;

    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "--picture")) {
            i += 1;
            if (i < argc) {
                pict = argv[i];
                break;
            }
        }
    }
    //if (pict) {
    //    SkString path(pict);
    //    fCurrIndex = fSamples.count();
    //    *fSamples.append() = new PictFileFactory(path);
    //}
}

void SampleWindow::registerPictFileSamples(char** argv, int argc) {
    const char* pictDir = NULL;

    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "--pictureDir")) {
            i += 1;
            if (i < argc) {
                pictDir = argv[i];
                break;
            }
        }
    }
    //if (pictDir) {
    //    SkOSFile::Iter iter(pictDir, "skp");
    //    SkString filename;
    //    while (iter.next(&filename)) {
    //        SkString path;
    //        make_filepath(&path, pictDir, filename);
    //        *fSamples.append() = new PictFileFactory(path);
    //    }
    //}
}

int SampleWindow::findByTitle(const char title[]) {
    int i, count = fSamples.count();
    for (i = 0; i < count; i++) {
        if (getSampleTitle(i).equals(title)) {
            return i;
        }
    }
    return -1;
}

void SampleWindow::listTitles() {
    int count = fSamples.count();
    for (int i = 0; i < count; i++) {
    }
}

static SkBitmap capture_bitmap(SkCanvas* canvas) {
    SkBitmap bm;
    const SkBitmap& src = canvas->getDevice()->accessBitmap(false);
    src.copyTo(&bm, src.config());
    return bm;
}

static bool bitmap_diff(SkCanvas* canvas, const SkBitmap& orig,
                        SkBitmap* diff) {
    const SkBitmap& src = canvas->getDevice()->accessBitmap(false);

    SkAutoLockPixels alp0(src);
    SkAutoLockPixels alp1(orig);
    for (int y = 0; y < src.height(); y++) {
        const void* srcP = src.getAddr(0, y);
        const void* origP = orig.getAddr(0, y);
        size_t bytes = src.width() * src.bytesPerPixel();
        if (memcmp(srcP, origP, bytes)) {
            return true;
        }
    }
    return false;
}

static void drawText(SkCanvas* canvas, SkString string, float left, float top, SkPaint& paint)
{
    SkColor desiredColor = paint.getColor();
    paint.setColor(SK_ColorWHITE);
    const char* c_str = string.c_str();
    size_t size = string.size();
    SkRect bounds;
    paint.measureText(c_str, size, &bounds);
    bounds.offset(left, top);
    float inset = SkIntToScalar(-2);
    bounds.inset(inset, inset);
    canvas->drawRect(bounds, paint);
    if (desiredColor != SK_ColorBLACK) {
        paint.setColor(SK_ColorBLACK);
        canvas->drawText(c_str, size, left + SK_Scalar1, top + SK_Scalar1, paint);
    }
    paint.setColor(desiredColor);
    canvas->drawText(c_str, size, left, top, paint);
}

#define XCLIP_N  8
#define YCLIP_N  8

void SampleWindow::draw(SkCanvas* canvas) {
    // update the animation time
    if (!gAnimTimePrev && !gAnimTime) {
        // first time make delta be 0
        gAnimTime = SkTime::GetMSecs();
        gAnimTimePrev = gAnimTime;
    } else {
        gAnimTimePrev = gAnimTime;
        gAnimTime = SkTime::GetMSecs();
    }

    if (fGesture.isActive()) {
        this->updateMatrix();
    }

    if (fMeasureFPS) {
        fMeasureFPS_Time = 0;
    }

    if (fNClip) {
        this->SkOSWindow::draw(canvas);
        SkBitmap orig = capture_bitmap(canvas);

        const float w = this->width();
        const float h = this->height();
        const float cw = w / XCLIP_N;
        const float ch = h / YCLIP_N;
        for (int y = 0; y < YCLIP_N; y++) {
            SkRect r;
            r.fTop = y * ch;
            r.fBottom = (y + 1) * ch;
            if (y == YCLIP_N - 1) {
                r.fBottom = h;
            }
            for (int x = 0; x < XCLIP_N; x++) {
                SkAutoCanvasRestore acr(canvas, true);
                r.fLeft = x * cw;
                r.fRight = (x + 1) * cw;
                if (x == XCLIP_N - 1) {
                    r.fRight = w;
                }
                canvas->clipRect(r);
                this->SkOSWindow::draw(canvas);
            }
        }
    }
	else
	{
        SkSize tile = this->tileSize();

        for (float y = 0; y < height(); y += tile.height())
		{
            for (float x = 0; x < width(); x += tile.width())
			{
                SkAutoCanvasRestore acr(canvas, true);
				canvas->clipRect(SkRect::MakeXYWH(x, y,
					tile.width(),
					tile.height()));
                this->SkOSWindow::draw(canvas);
            }
        }

        if (fTilingMode != kNo_Tiling) {
            SkPaint paint;
            paint.setColor(0x60FF00FF);
            paint.setStyle(SkPaint::kStroke_Style);

            for (float y = 0; y < height(); y += tile.height()) {
                for (float x = 0; x < width(); x += tile.width()) {
                    canvas->drawRect(SkRect::MakeXYWH(x, y,
                                                      tile.width(),
                                                      tile.height()),
                                     paint);
                }
            }
        }
    }
    if (fShowZoomer && !fSaveToPdf) {
        showZoomer(canvas);
    }
    if (fMagnify && !fSaveToPdf) {
        magnify(canvas);
    }

    if (fMeasureFPS && fMeasureFPS_Time) {
        this->updateTitle();
        this->postInvalDelay();
    }

    // do this last
    fDevManager->publishCanvas( canvas, this);
}

static float clipW = 200;
static float clipH = 200;
void SampleWindow::magnify(SkCanvas* canvas) {
    SkRect r;
    int count = canvas->save();

    SkMatrix m = canvas->getTotalMatrix();
    if (!m.invert(&m)) {
        return;
    }
    SkPoint offset, center;
    float mouseX = fMouseX * SK_Scalar1;
    float mouseY = fMouseY * SK_Scalar1;
    m.mapXY(mouseX - clipW/2, mouseY - clipH/2, &offset);
    m.mapXY(mouseX, mouseY, &center);

    r.set(0, 0, clipW * m.getScaleX(), clipH * m.getScaleX());
    r.offset(offset.fX, offset.fY);

    SkPaint paint;
    paint.setColor(0xFF66AAEE);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(10.f * m.getScaleX());
    //lense offset
    //canvas->translate(0, -250);
    canvas->drawRect(r, paint);
    canvas->clipRect(r);

    m = canvas->getTotalMatrix();
    m.setTranslate(-center.fX, -center.fY);
    m.postScale(0.5f * fFatBitsScale, 0.5f * fFatBitsScale);
    m.postTranslate(center.fX, center.fY);
    canvas->concat(m);

    this->SkOSWindow::draw(canvas);

    canvas->restoreToCount(count);
}

void SampleWindow::showZoomer(SkCanvas* canvas) {
        int count = canvas->save();
        canvas->resetMatrix();
        // Ensure the mouse position is on screen.
        int width = SkScalarRoundToInt(this->width());
        int height = SkScalarRoundToInt(this->height());
        if (fMouseX >= width) fMouseX = width - 1;
        else if (fMouseX < 0) fMouseX = 0;
        if (fMouseY >= height) fMouseY = height - 1;
        else if (fMouseY < 0) fMouseY = 0;

        SkBitmap bitmap = capture_bitmap(canvas);
        bitmap.lockPixels();

        // Find the size of the zoomed in view, forced to be odd, so the examined pixel is in the middle.
        int zoomedWidth = (width >> 1) | 1;
        int zoomedHeight = (height >> 1) | 1;
        SkIRect src;
        src.set(0, 0, zoomedWidth / fFatBitsScale, zoomedHeight / fFatBitsScale);
        src.offset(fMouseX - (src.width()>>1), fMouseY - (src.height()>>1));
        SkRect dest;
        dest.set(0, 0, SkIntToScalar(zoomedWidth), SkIntToScalar(zoomedHeight));
        dest.offset(SkIntToScalar(width - zoomedWidth), SkIntToScalar(height - zoomedHeight));
        SkPaint paint;
        // Clear the background behind our zoomed in view
        paint.setColor(SK_ColorWHITE);
        canvas->drawRect(dest, paint);
        canvas->drawBitmapRect(bitmap, &src, dest);
        paint.setColor(SK_ColorBLACK);
        paint.setStyle(SkPaint::kStroke_Style);
        // Draw a border around the pixel in the middle
        SkRect originalPixel;
        originalPixel.set(SkIntToScalar(fMouseX), SkIntToScalar(fMouseY), SkIntToScalar(fMouseX + 1), SkIntToScalar(fMouseY + 1));
        SkMatrix matrix;
        SkRect scalarSrc;
        scalarSrc.set(src);
        SkColor color = bitmap.getColor(fMouseX, fMouseY);
        if (matrix.setRectToRect(scalarSrc, dest, SkMatrix::kFill_ScaleToFit)) {
            SkRect pixel;
            matrix.mapRect(&pixel, originalPixel);
            // TODO Perhaps measure the values and make the outline white if it's "dark"
            if (color == SK_ColorBLACK) {
                paint.setColor(SK_ColorWHITE);
            }
            canvas->drawRect(pixel, paint);
        }
        paint.setColor(SK_ColorBLACK);
        // Draw a border around the destination rectangle
        canvas->drawRect(dest, paint);
        paint.setStyle(SkPaint::kStrokeAndFill_Style);
        // Identify the pixel and its color on screen
        paint.setTypeface(fTypeface);
        paint.setAntiAlias(true);
        float lineHeight = paint.getFontMetrics(NULL);
        SkString string;
        string.appendf("(%i, %i)", fMouseX, fMouseY);
        float left = dest.fLeft + SkIntToScalar(3);
        float i = SK_Scalar1;
        drawText(canvas, string, left, SkScalarMulAdd(lineHeight, i, dest.fTop), paint);
        // Alpha
        i += SK_Scalar1;
        string.reset();
        string.appendf("A: %X", SkColorGetA(color));
        drawText(canvas, string, left, SkScalarMulAdd(lineHeight, i, dest.fTop), paint);
        // Red
        i += SK_Scalar1;
        string.reset();
        string.appendf("R: %X", SkColorGetR(color));
        paint.setColor(SK_ColorRED);
        drawText(canvas, string, left, SkScalarMulAdd(lineHeight, i, dest.fTop), paint);
        // Green
        i += SK_Scalar1;
        string.reset();
        string.appendf("G: %X", SkColorGetG(color));
        paint.setColor(SK_ColorGREEN);
        drawText(canvas, string, left, SkScalarMulAdd(lineHeight, i, dest.fTop), paint);
        // Blue
        i += SK_Scalar1;
        string.reset();
        string.appendf("B: %X", SkColorGetB(color));
        paint.setColor(SK_ColorBLUE);
        drawText(canvas, string, left, SkScalarMulAdd(lineHeight, i, dest.fTop), paint);
        canvas->restoreToCount(count);
}

void SampleWindow::onDraw(SkCanvas* canvas) {
}

#include "SkColorPriv.h"

void SampleWindow::saveToPdf()
{
    fSaveToPdf = true;
    this->inval(NULL);
}

SkCanvas* SampleWindow::beforeChildren(SkCanvas* canvas) {

	{
        canvas = this->SkOSWindow::beforeChildren(canvas);
    }

    if (fUseClip) {
        canvas->drawColor(0xFFFF88FF);
        canvas->clipPath(fClipPath, SkRegion::kIntersect_Op, true);
    }

    return canvas;
}

static void paint_rgn(const SkBitmap& bm, const SkIRect& r,
                      const SkRegion& rgn) {
    SkCanvas    canvas(bm);
    SkRegion    inval(rgn);

    inval.translate(r.fLeft, r.fTop);
    canvas.clipRegion(inval);
    canvas.drawColor(0xFFFF8080);
}
#include "SkData.h"
void SampleWindow::afterChildren(SkCanvas* orig)
{
    if (fRequestGrabImage) {
        fRequestGrabImage = false;

        SkBaseDevice* device = orig->getDevice();
        SkBitmap bmp;
        if (device->accessBitmap(false).copyTo(&bmp, SkBitmap::kARGB_8888_Config)) {
            static int gSampleGrabCounter;
            SkString name;
            name.printf("sample_grab_%d.png", gSampleGrabCounter++);
            SkImageEncoder::EncodeFile(name.c_str(), bmp,
                                       SkImageEncoder::kPNG_Type, 100);
        }
    }

    // Do this after presentGL and other finishing, rather than in afterChild
    if (fMeasureFPS && fMeasureFPS_StartTime) {
        fMeasureFPS_Time += SkTime::GetMSecs() - fMeasureFPS_StartTime;
    }

    //    if ((fScrollTestX | fScrollTestY) != 0)
    if (false) {
        const SkBitmap& bm = orig->getDevice()->accessBitmap(true);
        int dx = fScrollTestX * 7;
        int dy = fScrollTestY * 7;
        SkIRect r;
        SkRegion inval;

        r.set(50, 50, 50+100, 50+100);
        bm.scrollRect(&r, dx, dy, &inval);
        paint_rgn(bm, r, inval);
    }
}

void SampleWindow::beforeChild(SkView* child, SkCanvas* canvas) {
    if (fRotate) {
        fRotateAnimTime += SampleCode::GetAnimSecondsDelta();

        float cx = this->width() / 2;
        float cy = this->height() / 2;
        canvas->translate(cx, cy);
        canvas->rotate(fRotateAnimTime * 10);
        canvas->translate(-cx, -cy);
    }

    if (fPerspAnim) {
        fPerspAnimTime += SampleCode::GetAnimSecondsDelta();

        static const float gAnimPeriod = 10 * SK_Scalar1;
        static const float gAnimMag = SK_Scalar1 / 1000;
        float t = SkScalarMod(fPerspAnimTime, gAnimPeriod);
        if (SkScalarFloorToInt(SkScalarDiv(fPerspAnimTime, gAnimPeriod)) & 0x1) {
            t = gAnimPeriod - t;
        }
        t = 2 * t - gAnimPeriod;
        t = SkScalarMul(SkScalarDiv(t, gAnimPeriod), gAnimMag);
        SkMatrix m;
        m.reset();
        m.setPerspY(t);
        canvas->concat(m);
    }

    this->installDrawFilter(canvas);

    if (fMeasureFPS) {
        if (SampleView::SetRepeatDraw(child, FPS_REPEAT_COUNT)) {
            fMeasureFPS_StartTime = SkTime::GetMSecs();
        }
    } else {
        (void)SampleView::SetRepeatDraw(child, 1);
    }
    if (fPerspAnim || fRotate) {
        this->inval(NULL);
    }
}

void SampleWindow::afterChild(SkView* child, SkCanvas* canvas) {
    canvas->setDrawFilter(NULL);
}

static SkBitmap::Config gConfigCycle[] = {
    SkBitmap::kNo_Config,           // none -> none
    SkBitmap::kNo_Config,           // a8 -> none
    SkBitmap::kNo_Config,           // index8 -> none
    SkBitmap::kARGB_4444_Config,    // 565 -> 4444
    SkBitmap::kARGB_8888_Config,    // 4444 -> 8888
    SkBitmap::kRGB_565_Config       // 8888 -> 565
};

static SkBitmap::Config cycle_configs(SkBitmap::Config c) {
    return gConfigCycle[c];
}

void SampleWindow::changeZoomLevel(float delta) {
    fZoomLevel += delta;
    if (fZoomLevel > 0) {
        fZoomLevel = SkMinScalar(fZoomLevel, MAX_ZOOM_LEVEL);
        fZoomScale = fZoomLevel + SK_Scalar1;
    } else if (fZoomLevel < 0) {
        fZoomLevel = SkMaxScalar(fZoomLevel, MIN_ZOOM_LEVEL);
        fZoomScale = SK_Scalar1 / (SK_Scalar1 - fZoomLevel);
    } else {
        fZoomScale = SK_Scalar1;
    }
    this->updateMatrix();
}

void SampleWindow::updateMatrix(){
    SkMatrix m;
    m.reset();
    if (fZoomLevel) {
        SkPoint center;
        //m = this->getLocalMatrix();//.invert(&m);
        m.mapXY(fZoomCenterX, fZoomCenterY, &center);
        float cx = center.fX;
        float cy = center.fY;

        m.setTranslate(-cx, -cy);
        m.postScale(fZoomScale, fZoomScale);
        m.postTranslate(cx, cy);
    }

    if (fFlipAxis) {
        m.preTranslate(fZoomCenterX, fZoomCenterY);
        if (fFlipAxis & kFlipAxis_X) {
            m.preScale(-SK_Scalar1, SK_Scalar1);
        }
        if (fFlipAxis & kFlipAxis_Y) {
            m.preScale(SK_Scalar1, -SK_Scalar1);
        }
        m.preTranslate(-fZoomCenterX, -fZoomCenterY);
        //canvas->concat(m);
    }
    // Apply any gesture matrix
    m.preConcat(fGesture.localM());
    m.preConcat(fGesture.globalM());

    this->setLocalMatrix(m);

    this->updateTitle();
    this->inval(NULL);
}
bool SampleWindow::previousSample() {
    fCurrIndex = (fCurrIndex - 1 + fSamples.count()) % fSamples.count();
    this->loadView(create_transition(curr_view(this), (*fSamples[fCurrIndex])(),
                                     fTransitionPrev));
    return true;
}

bool SampleWindow::nextSample() {
    fCurrIndex = (fCurrIndex + 1) % fSamples.count();
    this->loadView(create_transition(curr_view(this), (*fSamples[fCurrIndex])(),
                                     fTransitionNext));
    return true;
}

bool SampleWindow::goToSample(int i) {
    fCurrIndex = (i) % fSamples.count();
    this->loadView(create_transition(curr_view(this),(*fSamples[fCurrIndex])(), 6));
    return true;
}

SkString SampleWindow::getSampleTitle(int i) {
    return ::getSampleTitle(fSamples[i]);
}

int SampleWindow::sampleCount() {
    return fSamples.count();
}

void SampleWindow::showOverview() {
    this->loadView(create_transition(curr_view(this),
                                     create_overview(fSamples.count(), fSamples.begin()),
                                     4));
}

void SampleWindow::installDrawFilter(SkCanvas* canvas) {
    canvas->setDrawFilter(new FlagsDrawFilter(fLCDState, fAAState, fSubpixelState,
                                              fHintingState, fFilterLevelIndex))->unref();
}

void SampleWindow::postAnimatingEvent() {
    if (fAnimating) {
        (new SkEvent(ANIMATING_EVENTTYPE, this->getSinkID()))->postDelay(ANIMATING_DELAY);
    }
}

bool SampleWindow::onEvent(const SkEvent& evt) {
    if (evt.isType(gUpdateWindowTitleEvtName)) {
        this->updateTitle();
        return true;
    }
    if (evt.isType(ANIMATING_EVENTTYPE)) {
        if (fAnimating) {
            this->nextSample();
            this->postAnimatingEvent();
        }
        return true;
    }
    if (evt.isType("replace-transition-view")) {
        this->loadView((SkView*)SkEventSink::FindSink(evt.getFast32()));
        return true;
    }
    if (evt.isType("set-curr-index")) {
        this->goToSample(evt.getFast32());
        return true;
    }
    if (isInvalEvent(evt)) {
        this->inval(NULL);
        return true;
    }
    int selected = -1;
    if (SkOSMenu::FindListIndex(evt, "Device Type", &selected))
	{
        return true;
    }
    if (SkOSMenu::FindTriState(evt, "Pipe", &fPipeState)) {
#ifdef PIPE_NET
        if (!fPipeState != SkOSMenu::kOnState)
            gServer.disconnectAll();
#endif
        (void)SampleView::SetUsePipe(curr_view(this), fPipeState);
        this->updateTitle();
        this->inval(NULL);
        return true;
    }
    if (SkOSMenu::FindSwitchState(evt, "Slide Show", NULL)) {
        this->toggleSlideshow();
        return true;
    }
    if (SkOSMenu::FindTriState(evt, "AA", &fAAState) ||
        SkOSMenu::FindTriState(evt, "LCD", &fLCDState) ||
        SkOSMenu::FindListIndex(evt, "FilterLevel", &fFilterLevelIndex) ||
        SkOSMenu::FindTriState(evt, "Subpixel", &fSubpixelState) ||
        SkOSMenu::FindListIndex(evt, "Hinting", &fHintingState) ||
        SkOSMenu::FindSwitchState(evt, "Clip", &fUseClip) ||
        SkOSMenu::FindSwitchState(evt, "Zoomer", &fShowZoomer) ||
        SkOSMenu::FindSwitchState(evt, "Magnify", &fMagnify) ||
        SkOSMenu::FindListIndex(evt, "Transition-Next", &fTransitionNext) ||
        SkOSMenu::FindListIndex(evt, "Transition-Prev", &fTransitionPrev)) {
        this->inval(NULL);
        this->updateTitle();
        return true;
    }
    if (SkOSMenu::FindListIndex(evt, "Tiling", &fTilingMode)) {
        if (SampleView::IsSampleView(curr_view(this))) {
            ((SampleView*)curr_view(this))->onTileSizeChanged(this->tileSize());
        }
        this->inval(NULL);
        this->updateTitle();
        return true;
    }
    if (SkOSMenu::FindSwitchState(evt, "Flip X", NULL)) {
        fFlipAxis ^= kFlipAxis_X;
        this->updateMatrix();
        return true;
    }
    if (SkOSMenu::FindSwitchState(evt, "Flip Y", NULL)) {
        fFlipAxis ^= kFlipAxis_Y;
        this->updateMatrix();
        return true;
    }
    if (SkOSMenu::FindAction(evt,"Save to PDF")) {
        this->saveToPdf();
        return true;
    }
    return this->SkOSWindow::onEvent(evt);
}

bool SampleWindow::onQuery(SkEvent* query) {
    if (query->isType("get-slide-count")) {
        query->setFast32(fSamples.count());
        return true;
    }
    if (query->isType("get-slide-title")) {
        SkView* view = (*fSamples[query->getFast32()])();
        SkEvent evt(gTitleEvtName);
        if (view->doQuery(&evt)) {
            query->setString("title", evt.findString(gTitleEvtName));
        }
        SkSafeUnref(view);
        return true;
    }
    if (query->isType("use-fast-text")) {
        SkEvent evt(gFastTextEvtName);
        return curr_view(this)->doQuery(&evt);
    }
    if (query->isType("ignore-window-bitmap")) {
        query->setFast32(this->getGrContext() != NULL);
        return true;
    }
    return this->SkOSWindow::onQuery(query);
}

bool SampleWindow::onHandleChar(SkUnichar uni) {
    {
        SkView* view = curr_view(this);
        if (view) {
            SkEvent evt(gCharEvtName);
            evt.setFast32(uni);
            if (view->doQuery(&evt)) {
                return true;
            }
        }
    }

    int dx = 0xFF;
    int dy = 0xFF;

    switch (uni) {
        case '5': dx =  0; dy =  0; break;
        case '8': dx =  0; dy = -1; break;
        case '6': dx =  1; dy =  0; break;
        case '2': dx =  0; dy =  1; break;
        case '4': dx = -1; dy =  0; break;
        case '7': dx = -1; dy = -1; break;
        case '9': dx =  1; dy = -1; break;
        case '3': dx =  1; dy =  1; break;
        case '1': dx = -1; dy =  1; break;

        default:
            break;
    }

    if (0xFF != dx && 0xFF != dy) {
        if ((dx | dy) == 0) {
            fScrollTestX = fScrollTestY = 0;
        } else {
            fScrollTestX += dx;
            fScrollTestY += dy;
        }
        this->inval(NULL);
        return true;
    }

    switch (uni) {
        case 'B':
            post_event_to_sink(SkNEW_ARGS(SkEvent, ("PictFileView::toggleBBox")), curr_view(this));
            // Cannot call updateTitle() synchronously, because the toggleBBox event is still in
            // the queue.
            post_event_to_sink(SkNEW_ARGS(SkEvent, (gUpdateWindowTitleEvtName)), this);
            this->inval(NULL);
            break;
        case 'f':
            // only
            toggleFPS();
            break;
        case 'g':
            fRequestGrabImage = true;
            this->inval(NULL);
            break;
        case 'G':
            gShowGMBounds = !gShowGMBounds;
            post_event_to_sink(GMSampleView::NewShowSizeEvt(gShowGMBounds),
                            curr_view(this));
            this->inval(NULL);
            break;
        case 'i':
            this->zoomIn();
            break;
        case 'o':
            this->zoomOut();
            break;
        case 'r':
            fRotate = !fRotate;
            fRotateAnimTime = 0;
            this->inval(NULL);
            this->updateTitle();
            return true;
        case 'k':
            fPerspAnim = !fPerspAnim;
            this->inval(NULL);
            this->updateTitle();
            return true;
#if SK_SUPPORT_GPU
        case '\\':
            this->inval(NULL);
            this->updateTitle();
            return true;
        case 'p':
            {
                GrContext* grContext = this->getGrContext();
                if (grContext) {
                    grContext->freeGpuResources();
                }
            }
            return true;
#endif
        default:
            break;
    }

    if (fAppMenu->handleKeyEquivalent(uni)|| fSlideMenu->handleKeyEquivalent(uni)) {
        this->onUpdateMenu(fAppMenu);
        this->onUpdateMenu(fSlideMenu);
        return true;
    }
    return this->SkOSWindow::onHandleChar(uni);
}

void SampleWindow::toggleSlideshow() {
    fAnimating = !fAnimating;
    this->postAnimatingEvent();
    this->updateTitle();
}

void SampleWindow::toggleRendering() {
    this->updateTitle();
    this->inval(NULL);
}

void SampleWindow::toggleFPS() {
    fMeasureFPS = !fMeasureFPS;
    this->updateTitle();
    this->inval(NULL);
}

#include "SkDumpCanvas.h"

bool SampleWindow::onHandleKey(SkKey key) {
    {
        SkView* view = curr_view(this);
        if (view) {
            SkEvent evt(gKeyEvtName);
            evt.setFast32(key);
            if (view->doQuery(&evt)) {
                return true;
            }
        }
    }
    switch (key) {
        case kRight_SkKey:
            if (this->nextSample()) {
                return true;
            }
            break;
        case kLeft_SkKey:
            if (this->previousSample()) {
                return true;
            }
            return true;
        case kUp_SkKey:
            if (USE_ARROWS_FOR_ZOOM) {
                this->changeZoomLevel(1.f / 32.f);
            } else {
                fNClip = !fNClip;
                this->inval(NULL);
                this->updateTitle();
            }
            return true;
        case kDown_SkKey:
            if (USE_ARROWS_FOR_ZOOM) {
                this->changeZoomLevel(-1.f / 32.f);
            }            
			return true;
        case kOK_SkKey: {
            SkString title;
            if (curr_title(this, &title)) {
                writeTitleToPrefs(title.c_str());
            }
            return true;
        }
        case kBack_SkKey:
            this->showOverview();
            return true;
        default:
            break;
    }
    return this->SkOSWindow::onHandleKey(key);
}

///////////////////////////////////////////////////////////////////////////////

static const char gGestureClickType[] = "GestureClickType";

bool SampleWindow::onDispatchClick(int x, int y, Click::State state,
        void* owner, unsigned modi) {
    if (Click::kMoved_State == state) {
        updatePointer(x, y);
    }
    int w = SkScalarRoundToInt(this->width());
    int h = SkScalarRoundToInt(this->height());

    // check for the resize-box
    if (w - x < 16 && h - y < 16) {
        return false;   // let the OS handle the click
    }
    else if (fMagnify) {
        //it's only necessary to update the drawing if there's a click
        this->inval(NULL);
        return false; //prevent dragging while magnify is enabled
    } else {
        // capture control+option, and trigger debugger
        if ((modi & kControl_SkModifierKey) && (modi & kOption_SkModifierKey)) {
            if (Click::kDown_State == state) {
                SkEvent evt("debug-hit-test");
                evt.setS32("debug-hit-test-x", x);
                evt.setS32("debug-hit-test-y", y);
                curr_view(this)->doEvent(evt);
            }
            return true;
        } else {
            return this->SkOSWindow::onDispatchClick(x, y, state, owner, modi);
        }
    }
}

class GestureClick : public SkView::Click {
public:
    GestureClick(SkView* target) : SkView::Click(target) {
        this->setType(gGestureClickType);
    }

    static bool IsGesture(Click* click) {
        return click->isType(gGestureClickType);
    }
};

SkView::Click* SampleWindow::onFindClickHandler(float x, float y,
                                                unsigned modi) {
    return new GestureClick(this);
}

bool SampleWindow::onClick(Click* click) {
    if (GestureClick::IsGesture(click)) {
        float x = static_cast<float>(click->fICurr.fX);
        float y = static_cast<float>(click->fICurr.fY);

        switch (click->fState) {
            case SkView::Click::kDown_State:
                fGesture.touchBegin(click->fOwner, x, y);
                break;
            case SkView::Click::kMoved_State:
                fGesture.touchMoved(click->fOwner, x, y);
                this->updateMatrix();
                break;
            case SkView::Click::kUp_State:
                fGesture.touchEnd(click->fOwner);
                this->updateMatrix();
                break;
        }
        return true;
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////

void SampleWindow::loadView(SkView* view) {
    SkView::F2BIter iter(this);
    SkView* prev = iter.next();
    if (prev) {
        prev->detachFromParent();
    }

    view->setVisibleP(true);
    view->setClipToBounds(false);
    this->attachChildToFront(view)->unref();
    view->setSize(this->width(), this->height());

    //repopulate the slide menu when a view is loaded
    fSlideMenu->reset();

    (void)SampleView::SetUsePipe(view, fPipeState);
    if (SampleView::IsSampleView(view)) {
        SampleView* sampleView = (SampleView*)view;
        sampleView->requestMenu(fSlideMenu);
        sampleView->onTileSizeChanged(this->tileSize());
    }
    this->onUpdateMenu(fSlideMenu);
    this->updateTitle();
}

static const char* gConfigNames[] = {
    "unknown config",
    "A8",
    "Index8",
    "565",
    "4444",
    "8888"
};

static const char* configToString(SkBitmap::Config c) {
    return gConfigNames[c];
}

static const char* trystate_str(SkOSMenu::TriState state,
                                const char trueStr[], const char falseStr[]) {
    if (SkOSMenu::kOnState == state) {
        return trueStr;
    } else if (SkOSMenu::kOffState == state) {
        return falseStr;
    }
    return NULL;
}

void SampleWindow::updateTitle() {
    SkView* view = curr_view(this);

    SkString title;
    if (!curr_title(this, &title)) {
        title.set("<unknown>");
    }
	title.prepend(" ");
    //title.prepend(configToString(this->getBitmap().config()));

    if (fTilingMode != kNo_Tiling) {
        title.prependf("<T: %s> ", gTilingInfo[fTilingMode].label);
    }
    if (fAnimating) {
        title.prepend("<A> ");
    }
    if (fRotate) {
        title.prepend("<R> ");
    }
    if (fNClip) {
        title.prepend("<C> ");
    }
    if (fPerspAnim) {
        title.prepend("<K> ");
    }

    title.prepend(trystate_str(fLCDState, "LCD ", "lcd "));
    title.prepend(trystate_str(fAAState, "AA ", "aa "));
    title.prepend(gFilterLevelStates[fFilterLevelIndex].fLabel);
    title.prepend(trystate_str(fSubpixelState, "S ", "s "));
    title.prepend(fFlipAxis & kFlipAxis_X ? "X " : NULL);
    title.prepend(fFlipAxis & kFlipAxis_Y ? "Y " : NULL);
    title.prepend(gHintingStates[fHintingState].label);

    if (fZoomLevel) {
        title.prependf("{%.2f} ", SkScalarToFloat(fZoomLevel));
    }

    if (fMeasureFPS) {
        title.appendf(" %8.3f ms", fMeasureFPS_Time / (float)FPS_REPEAT_COUNT);
    }
    if (SampleView::IsSampleView(view)) {
        switch (fPipeState) {
            case SkOSMenu::kOnState:
                title.prepend("<Pipe> ");
                break;
            case SkOSMenu::kMixedState:
                title.prepend("<Tiled Pipe> ");
                break;

            default:
                break;
        }
        title.prepend("! ");
    }

#if SK_SUPPORT_GPU
    if (  NULL != fDevManager &&
        fDevManager->getGrRenderTarget() &&
        fDevManager->getGrRenderTarget()->numSamples() > 0) 
	{
        title.appendf(" [MSAA: %d]",
                       fDevManager->getGrRenderTarget()->numSamples());
    }
#endif

    this->setTitle(title.c_str());
}

void SampleWindow::onSizeChange() {
    this->SkOSWindow::onSizeChange();

    SkView::F2BIter iter(this);
    SkView* view = iter.next();
    view->setSize(this->width(), this->height());

    // rebuild our clippath
    {
        const float W = this->width();
        const float H = this->height();

        fClipPath.reset();

        SkRect r;
        r.set(0, 0, W, H);
        fClipPath.addRect(r, SkPath::kCCW_Direction);
        r.set(W/4, H/4, W*3/4, H*3/4);
        fClipPath.addRect(r, SkPath::kCW_Direction);
    }

    fZoomCenterX = SkScalarHalf(this->width());
    fZoomCenterY = SkScalarHalf(this->height());

#ifdef SK_BUILD_FOR_ANDROID
    // FIXME: The first draw after a size change does not work on Android, so
    // we post an invalidate.
    this->postInvalDelay();
#endif
    this->updateTitle();    // to refresh our config
    fDevManager->windowSizeChanged(this);

    if (fTilingMode != kNo_Tiling && SampleView::IsSampleView(view)) {
        ((SampleView*)view)->onTileSizeChanged(this->tileSize());
    }
}

///////////////////////////////////////////////////////////////////////////////

static const char is_sample_view_tag[] = "sample-is-sample-view";
static const char repeat_count_tag[] = "sample-set-repeat-count";
static const char set_use_pipe_tag[] = "sample-set-use-pipe";

bool SampleView::IsSampleView(SkView* view) {
    SkEvent evt(is_sample_view_tag);
    return view->doQuery(&evt);
}

bool SampleView::SetRepeatDraw(SkView* view, int count) {
    SkEvent evt(repeat_count_tag);
    evt.setFast32(count);
    return view->doEvent(evt);
}

bool SampleView::SetUsePipe(SkView* view, SkOSMenu::TriState state) {
    SkEvent evt;
    evt.setS32(set_use_pipe_tag, state);
    return view->doEvent(evt);
}

bool SampleView::onEvent(const SkEvent& evt) {
    if (evt.isType(repeat_count_tag)) {
        fRepeatCount = evt.getFast32();
        return true;
    }

    int32_t pipeHolder;
    if (evt.findS32(set_use_pipe_tag, &pipeHolder)) {
        fPipeState = static_cast<SkOSMenu::TriState>(pipeHolder);
        return true;
    }

    if (evt.isType("debug-hit-test")) {
        fDebugHitTest = true;
        evt.findS32("debug-hit-test-x", &fDebugHitTestLoc.fX);
        evt.findS32("debug-hit-test-y", &fDebugHitTestLoc.fY);
        this->inval(NULL);
        return true;
    }

    return this->INHERITED::onEvent(evt);
}

bool SampleView::onQuery(SkEvent* evt) {
    if (evt->isType(is_sample_view_tag)) {
        return true;
    }
    return this->INHERITED::onQuery(evt);
}


class SimplePC : public SkGPipeController {
public:
    SimplePC(SkCanvas* target);
    ~SimplePC();

    virtual void* requestBlock(size_t minRequest, size_t* actual);
    virtual void notifyWritten(size_t bytes);

private:
    SkGPipeReader   fReader;
    void*           fBlock;
    size_t          fBlockSize;
    size_t          fBytesWritten;
    int             fAtomsWritten;
    SkGPipeReader::Status   fStatus;

    size_t        fTotalWritten;
};

SimplePC::SimplePC(SkCanvas* target) : fReader(target) {
    fBlock = NULL;
    fBlockSize = fBytesWritten = 0;
    fStatus = SkGPipeReader::kDone_Status;
    fTotalWritten = 0;
    fAtomsWritten = 0;
    fReader.setBitmapDecoder(&SkImageDecoder::DecodeMemory);
}

SimplePC::~SimplePC() {
    sk_free(fBlock);
}

void* SimplePC::requestBlock(size_t minRequest, size_t* actual) {
    sk_free(fBlock);

    fBlockSize = minRequest * 4;
    fBlock = sk_malloc_throw(fBlockSize);
    fBytesWritten = 0;
    *actual = fBlockSize;
    return fBlock;
}

void SimplePC::notifyWritten(size_t bytes) {
    fStatus = fReader.playback((const char*)fBlock + fBytesWritten, bytes);
    fBytesWritten += bytes;
    fTotalWritten += bytes;

    fAtomsWritten += 1;
}

void SampleView::draw(SkCanvas* canvas) {
    if (SkOSMenu::kOffState == fPipeState) {
        this->INHERITED::draw(canvas);
    } else {
        SkGPipeWriter writer;
        SimplePC controller(canvas);
        TiledPipeController tc(canvas->getDevice()->accessBitmap(false),
                               &SkImageDecoder::DecodeMemory,
                               &canvas->getTotalMatrix());
        SkGPipeController* pc;
        if (SkOSMenu::kMixedState == fPipeState) {
            pc = &tc;
        } else {
            pc = &controller;
        }
        uint32_t flags = SkGPipeWriter::kCrossProcess_Flag;

        canvas = writer.startRecording(pc, flags);
        //Must draw before controller goes out of scope and sends data
        this->INHERITED::draw(canvas);
        //explicitly end recording to ensure writer is flushed before the memory
        //is freed in the deconstructor of the controller
        writer.endRecording();
    }
}

#include "SkBounder.h"

class DebugHitTestBounder : public SkBounder {
public:
    DebugHitTestBounder(int x, int y) {
        fLoc.set(x, y);
    }

    virtual bool onIRect(const SkIRect& bounds) SK_OVERRIDE {
        if (bounds.contains(fLoc.x(), fLoc.y())) {
            //
            // Set a break-point here to see what was being drawn under
            // the click point (just needed a line of code to stop the debugger)
            //
            bounds.centerX();
        }
        return true;
    }

private:
    SkIPoint fLoc;
    typedef SkBounder INHERITED;
};

void SampleView::onDraw(SkCanvas* canvas) {
    this->onDrawBackground(canvas);

    DebugHitTestBounder bounder(fDebugHitTestLoc.x(), fDebugHitTestLoc.y());
    if (fDebugHitTest) {
        canvas->setBounder(&bounder);
    }

    for (int i = 0; i < fRepeatCount; i++) {
        SkAutoCanvasRestore acr(canvas, true);
        this->onDrawContent(canvas);
    }

    fDebugHitTest = false;
    canvas->setBounder(NULL);
}

void SampleView::onDrawBackground(SkCanvas* canvas) {
    canvas->drawColor(fBGColor);
}

///////////////////////////////////////////////////////////////////////////////

template <typename T> void SkTBSort(T array[], int count) {
    for (int i = 1; i < count - 1; i++) {
        bool didSwap = false;
        for (int j = count - 1; j > i; --j) {
            if (array[j] < array[j-1]) {
                T tmp(array[j-1]);
                array[j-1] = array[j];
                array[j] = tmp;
                didSwap = true;
            }
        }
        if (!didSwap) {
            break;
        }
    }
}

#include "SkRandom.h"

static void rand_rect(SkIRect* rect, SkRandom& rand) {
    int bits = 8;
    int shift = 32 - bits;
    rect->set(rand.nextU() >> shift, rand.nextU() >> shift,
              rand.nextU() >> shift, rand.nextU() >> shift);
    rect->sort();
}

static void dumpRect(const SkIRect& r) {
}

static void test_rects(const SkIRect rect[], int count) {
    SkRegion rgn0, rgn1;

    for (int i = 0; i < count; i++) {
        rgn0.op(rect[i], SkRegion::kUnion_Op);
     //   dumpRect(rect[i]);
    }
    rgn1.setRects(rect, count);

    if (rgn0 != rgn1) {
        for (int i = 0; i < count; i++) {
            dumpRect(rect[i]);
        }
    }
}

static void test() {
    size_t i;

    const SkIRect r0[] = {
        { 0, 0, 1, 1 },
        { 2, 2, 3, 3 },
    };
    const SkIRect r1[] = {
        { 0, 0, 1, 3 },
        { 1, 1, 2, 2 },
        { 2, 0, 3, 3 },
    };
    const SkIRect r2[] = {
        { 0, 0, 1, 2 },
        { 2, 1, 3, 3 },
        { 4, 0, 5, 1 },
        { 6, 0, 7, 4 },
    };

    static const struct {
        const SkIRect* fRects;
        int            fCount;
    } gRecs[] = {
        { r0, SK_ARRAY_COUNT(r0) },
        { r1, SK_ARRAY_COUNT(r1) },
        { r2, SK_ARRAY_COUNT(r2) },
    };

    for (i = 0; i < SK_ARRAY_COUNT(gRecs); i++) {
        test_rects(gRecs[i].fRects, gRecs[i].fCount);
    }

    SkRandom rand;
    for (i = 0; i < 10000; i++) {
        SkRegion rgn0, rgn1;

        const int N = 8;
        SkIRect rect[N];
        for (int j = 0; j < N; j++) {
            rand_rect(&rect[j], rand);
        }
        test_rects(rect, N);
    }
}

SkOSWindow* create_sk_window(void* hwnd, int argc, char** argv)
{
    if (false) { // avoid bit rot, suppress warning
        test();
    }
    return new SampleWindow(hwnd, argc, argv, NULL);
}

// FIXME: this should be in a header
void get_preferred_size(int* x, int* y, int* width, int* height);
void get_preferred_size(int* x, int* y, int* width, int* height) {
    *x = 10;
    *y = 50;
    *width = 640;
    *height = 480;
}

#ifdef SK_BUILD_FOR_IOS
void save_args(int argc, char *argv[]) {
}
#endif

// FIXME: this should be in a header
void application_init()
{
//    setenv("ANDROID_ROOT", "../../../data", 0);
#ifdef SK_BUILD_FOR_MAC
    setenv("ANDROID_ROOT", "/android/device/data", 0);
#endif
    SkGraphics::Init();
    SkEvent::Init();
}

// FIXME: this should be in a header
void application_term();
void application_term() {
    SkEvent::Term();
    SkGraphics::Term();
}
