/*
 * SkiaApp.h
 *
 *  Created on: 2015-7-28
 *      Author: jkd2972
 */

#ifndef SKIAAPP_H_
#define SKIAAPP_H_
#include "GrContext.h"
#include <string>


namespace egret {

class SkiaApp {
protected:
	static SkiaApp * _instance;
	static std::string filesDir;
	GrContext * fCurContext;
	GrRenderTarget * fCurRenderTarget;
	SkCanvas * canvas;
	SkBitmap bitmap;
public:
	SkiaApp();
	virtual ~SkiaApp();

	static SkiaApp * createSkiaApp();

	static SkiaApp * getSkiaApp();

	static void setFilesDir(const std::string &filesDir);

	void initApp(int width , int height);

	void pauseApp();
	void resumeApp();

	void DrawTest(SkCanvas *canvas );
	void TestArc(SkCanvas *canvas );
	void TestFillStyle( SkCanvas *canvas );
	void TestShadow( SkCanvas *canvas );
	void TestScale(SkCanvas *canvas );
	void TestCopyBitmap( SkCanvas *canvas );
	void TestDrawImage(SkCanvas *canvas );
	void TestFill( SkCanvas *canvas);
	void TestAddColorStop( SkCanvas *canvas );
	void TestArcTo( SkCanvas *canvas );
	void TestText( SkCanvas *canvas );
	void TestGetImageData( SkCanvas *canvas);
	void TestCreateRadialGradient( SkCanvas *canvas );
	void TestShadowOffset( SkCanvas *canvas );
	void TestCreatePattern( SkCanvas *canvas );
	void mainLoop();

	void windowChanged(int width,int height);
	SkCanvas* createCanvas();

	bool createBitmap(const std::string &src);
};

} /* namespace egret */
#endif /* SKIAAPP_H_ */
