/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SampleCode.h"
#include "SkView.h"
#include "SkCanvas.h"
#include "SkDevice.h"
#include "SkColorPriv.h"
#include "SkShader.h"
#include "CanvasRenderingContext2D.h"
using namespace WebCore;
using namespace WTF;


class RectView : public SampleView
{
    SkPaint fBGPaint;
public:
	RectView() 
	{
    }

protected:
    // overrides from SkEventSink
    virtual bool onQuery(SkEvent* evt)
	{
        if (SampleCode::TitleQ(*evt)) 
		{
            SampleCode::TitleR(evt, "Rect");
            return true;
        }
        return this->INHERITED::onQuery(evt);
    }

    virtual void onDrawContent(SkCanvas* canvas)
	{
		PassOwnPtr<CanvasRenderingContext2D> ctx = CanvasRenderingContext2D::create(canvas, NULL, false);
		ctx->beginPath();
		ctx->setLineWidth(6);
		ctx->setStrokeColor("red");
		ctx->rect(5, 5, 290, 140);
		ctx->stroke();

		ctx->beginPath();
		ctx->setLineWidth(4);
		ctx->setStrokeColor("green");
		ctx->rect(30, 30, 50, 50);
		ctx->stroke();

		ctx->beginPath();
		ctx->setLineWidth(10);
		ctx->setStrokeColor("blue");
		ctx->rect(50, 50, 150, 80);
		ctx->stroke();

    }

private:
    typedef SampleView INHERITED;
};

///////////////////////////////////////////////////////////////////////////////

static SkView* MyFactory() { return new RectView; }
static SkViewRegister reg(MyFactory);
