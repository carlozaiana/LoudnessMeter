#include "LoudnessHistoryDisplay.h"
#include <cmath>
#include <algorithm>

LoudnessHistoryDisplay::LoudnessHistoryDisplay(LoudnessDataStore& store)
    : dataStore(store)
{
    setOpaque(true);
    initializeBitmaps();
    startTimerHz(60);
}

LoudnessHistoryDisplay::~LoudnessHistoryDisplay()
{
    stopTimer();
}

void LoudnessHistoryDisplay::initializeBitmaps()
{
    for (int lod = 0; lod < kNumLods; ++lod)
    {
        const auto& cfg = kLodConfigs[static_cast<size_t>(lod)];
        auto& bmp = lodBitmaps[static_cast<size_t>(lod)];
        
        bmp.image = juce::Image(juce::Image::ARGB, cfg.bitmapWidth, kBitmapHeight, true);
        bmp.image.clear(bmp.image.getBounds(), bgColour);
        
        bmp.tempChunk = juce::Image(juce::Image::ARGB, kChunkSize + 4, kBitmapHeight, true);
        
        bmp.renderedUpToTime = 0.0;
        bmp.totalColumnsRendered = 0;
    }
}

void LoudnessHistoryDisplay::timerCallback()
{
    updateBitmaps();
    repaint();
}

void LoudnessHistoryDisplay::updateBitmaps()
{
    double dataTime = dataStore.getCurrentTime();
    double renderTime = dataTime - kDisplayDelay;
    
    if (renderTime <= 0.0)
        return;
    
    for (int lod = 0; lod < kNumLods; ++lod)
    {
        const auto& cfg = kLodConfigs[static_cast<size_t>(lod)];
        auto& bmp = lodBitmaps[static_cast<size_t>(lod)];
        
        double chunkDuration = cfg.secondsPerPixel * kChunkSize;
        
        while (bmp.renderedUpToTime + chunkDuration <= renderTime)
        {
            double chunkStart = bmp.renderedUpToTime;
            double chunkEnd = chunkStart + chunkDuration;
            
            renderChunkToBitmap(lod, chunkStart, chunkEnd);
            
            bmp.renderedUpToTime = chunkEnd;
        }
    }
}

void LoudnessHistoryDisplay::renderChunkToBitmap(int lodLevel, double startTime, double endTime)
{
    const auto& cfg = kLodConfigs[static_cast<size_t>(lodLevel)];
    auto& bmp = lodBitmaps[static_cast<size_t>(lodLevel)];
    
    double overlap = cfg.secondsPerPixel * 2.0;
    auto points = dataStore.getPointsInRange(startTime - overlap, endTime + overlap);
    
    bmp.tempChunk.clear(bmp.tempChunk.getBounds(), bgColour);
    
    if (points.empty())
    {
        blitTempToRingBuffer(lodLevel, bmp.totalColumnsRendered % cfg.bitmapWidth, kChunkSize);
        bmp.totalColumnsRendered += kChunkSize;
        return;
    }
    
    juce::Graphics g(bmp.tempChunk);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    
    int tempWidth = kChunkSize + 4;
    
    struct PixelData
    {
        float mMin{100.0f}, mMax{-100.0f};
        float sMin{100.0f}, sMax{-100.0f};
        bool valid{false};
    };
    
    std::vector<PixelData> pixelData(static_cast<size_t>(tempWidth));
    
    double chunkPixelStart = startTime / cfg.secondsPerPixel;
    
    for (const auto& pt : points)
    {
        double pixelPos = pt.timestamp / cfg.secondsPerPixel - chunkPixelStart + 2.0;
        int pixelIdx = static_cast<int>(std::floor(pixelPos));
        
        if (pixelIdx >= 0 && pixelIdx < tempWidth)
        {
            auto& pd = pixelData[static_cast<size_t>(pixelIdx)];
            
            if (pt.momentary > -100.0f)
            {
                pd.mMin = std::min(pd.mMin, pt.momentary);
                pd.mMax = std::max(pd.mMax, pt.momentary);
                pd.valid = true;
            }
            if (pt.shortTerm > -100.0f)
            {
                pd.sMin = std::min(pd.sMin, pt.shortTerm);
                pd.sMax = std::max(pd.sMax, pt.shortTerm);
                pd.valid = true;
            }
        }
    }
    
    juce::Path mFillPath, mLinePath;
    juce::Path sFillPath, sLinePath;
    
    bool mStarted = false, sStarted = false;
    float lastMX = 0, lastMY = 0;
    float lastSX = 0, lastSY = 0;
    
    std::vector<juce::Point<float>> mTopPts, mBotPts;
    std::vector<juce::Point<float>> sTopPts, sBotPts;
    
    for (int i = 0; i < tempWidth; ++i)
    {
        const auto& pd = pixelData[static_cast<size_t>(i)];
        float x = static_cast<float>(i) + 0.5f;
        
        if (pd.valid && pd.mMax > -100.0f)
        {
            float yTop = lufsToNormalizedY(pd.mMax) * static_cast<float>(kBitmapHeight);
            float yBot = lufsToNormalizedY(pd.mMin) * static_cast<float>(kBitmapHeight);
            float yMid = (yTop + yBot) * 0.5f;
            
            yTop = juce::jlimit(0.0f, static_cast<float>(kBitmapHeight), yTop);
            yBot = juce::jlimit(0.0f, static_cast<float>(kBitmapHeight), yBot);
            yMid = juce::jlimit(0.0f, static_cast<float>(kBitmapHeight), yMid);
            
            mTopPts.push_back({x, yTop});
            mBotPts.push_back({x, yBot});
            
            if (!mStarted)
            {
                mLinePath.startNewSubPath(x, yMid);
                mStarted = true;
            }
            else
            {
                mLinePath.lineTo(x, yMid);
            }
            lastMX = x;
            lastMY = yMid;
        }
        
        if (pd.valid && pd.sMax > -100.0f)
        {
            float yTop = lufsToNormalizedY(pd.sMax) * static_cast<float>(kBitmapHeight);
            float yBot = lufsToNormalizedY(pd.sMin) * static_cast<float>(kBitmapHeight);
            float yMid = (yTop + yBot) * 0.5f;
            
            yTop = juce::jlimit(0.0f, static_cast<float>(kBitmapHeight), yTop);
            yBot = juce::jlimit(0.0f, static_cast<float>(kBitmapHeight), yBot);
            yMid = juce::jlimit(0.0f, static_cast<float>(kBitmapHeight), yMid);
            
            sTopPts.push_back({x, yTop});
            sBotPts.push_back({x, yBot});
            
            if (!sStarted)
            {
                sLinePath.startNewSubPath(x, yMid);
                sStarted = true;
            }
            else
            {
                sLinePath.lineTo(x, yMid);
            }
            lastSX = x;
            lastSY = yMid;
        }
    }
    
    if (mTopPts.size() >= 2)
    {
        mFillPath.startNewSubPath(mTopPts[0]);
        for (size_t i = 1; i < mTopPts.size(); ++i)
            mFillPath.lineTo(mTopPts[i]);
        for (auto it = mBotPts.rbegin(); it != mBotPts.rend(); ++it)
            mFillPath.lineTo(*it);
        mFillPath.closeSubPath();
    }
    
    if (sTopPts.size() >= 2)
    {
        sFillPath.startNewSubPath(sTopPts[0]);
        for (size_t i = 1; i < sTopPts.size(); ++i)
            sFillPath.lineTo(sTopPts[i]);
        for (auto it = sBotPts.rbegin(); it != sBotPts.rend(); ++it)
            sFillPath.lineTo(*it);
        sFillPath.closeSubPath();
    }
    
    if (!mFillPath.isEmpty())
    {
        g.setColour(momentaryColour.withAlpha(0.5f));
        g.fillPath(mFillPath);
    }
    if (!mLinePath.isEmpty())
    {
        g.setColour(momentaryColour);
        g.strokePath(mLinePath, juce::PathStrokeType(2.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    if (!sFillPath.isEmpty())
    {
        g.setColour(shortTermColour.withAlpha(0.6f));
        g.fillPath(sFillPath);
    }
    if (!sLinePath.isEmpty())
    {
        g.setColour(shortTermColour);
        g.strokePath(sLinePath, juce::PathStrokeType(2.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    blitTempToRingBuffer(lodLevel, bmp.totalColumnsRendered % cfg.bitmapWidth, kChunkSize);
    bmp.totalColumnsRendered += kChunkSize;
}

void LoudnessHistoryDisplay::blitTempToRingBuffer(int lodLevel, int startColumn, int numColumns)
{
    const auto& cfg = kLodConfigs[static_cast<size_t>(lodLevel)];
    auto& bmp = lodBitmaps[static_cast<size_t>(lodLevel)];
    
    int srcX = 2;
    
    if (startColumn + numColumns <= cfg.bitmapWidth)
    {
        juce::Graphics g(bmp.image);
        g.drawImage(bmp.tempChunk,
                    startColumn, 0, numColumns, kBitmapHeight,
                    srcX, 0, numColumns, kBitmapHeight);
    }
    else
    {
        int part1 = cfg.bitmapWidth - startColumn;
        int part2 = numColumns - part1;
        
        {
            juce::Graphics g(bmp.image);
            g.drawImage(bmp.tempChunk,
                        startColumn, 0, part1, kBitmapHeight,
                        srcX, 0, part1, kBitmapHeight);
        }
        
        {
            juce::Graphics g(bmp.image);
            g.drawImage(bmp.tempChunk,
                        0, 0, part2, kBitmapHeight,
                        srcX + part1, 0, part2, kBitmapHeight);
        }
    }
}

float LoudnessHistoryDisplay::lufsToNormalizedY(float lufs) const
{
    return (kImageMaxLufs - lufs) / kImageLufsRange;
}

int LoudnessHistoryDisplay::getLodForTimeRange(double timeRange) const
{
    for (int lod = 0; lod < kNumLods; ++lod)
    {
        if (timeRange <= kLodConfigs[static_cast<size_t>(lod)].maxTimeRange)
            return lod;
    }
    return kNumLods - 1;
}

void LoudnessHistoryDisplay::setCurrentLoudness(float momentary, float shortTerm)
{
    currentMomentary = momentary;
    currentShortTerm = shortTerm;
}

void LoudnessHistoryDisplay::paint(juce::Graphics& g)
{
    drawBackground(g);
    drawBitmapToScreen(g);
    drawGrid(g);
    drawCurrentValues(g);
    drawZoomInfo(g);
}

void LoudnessHistoryDisplay::drawBackground(juce::Graphics& g)
{
    g.fillAll(bgColour);
}

void LoudnessHistoryDisplay::drawBitmapToScreen(juce::Graphics& g)
{
    int w = getWidth();
    int h = getHeight();
    
    if (w <= 0 || h <= 0)
        return;
    
    double dataTime = dataStore.getCurrentTime();
    double displayEndTime = dataTime - kDisplayDelay;
    
    if (displayEndTime <= 0.0)
        return;
    
    double displayStartTime = displayEndTime - viewTimeRange;
    
    activeLod = getLodForTimeRange(viewTimeRange);
    const auto& cfg = kLodConfigs[static_cast<size_t>(activeLod)];
    const auto& bmp = lodBitmaps[static_cast<size_t>(activeLod)];
    
    if (bmp.totalColumnsRendered == 0)
        return;
    
    double endPixelExact = displayEndTime / cfg.secondsPerPixel;
    double startPixelExact = displayStartTime / cfg.secondsPerPixel;
    
    if (startPixelExact < 0)
        startPixelExact = 0;
    
    double pixelsToShow = endPixelExact - startPixelExact;
    if (pixelsToShow <= 0)
        return;
    
    float subPixelOffset = static_cast<float>(endPixelExact - std::floor(endPixelExact));
    
    int endPixel = static_cast<int>(std::floor(endPixelExact));
    int startPixel = static_cast<int>(std::floor(startPixelExact));
    int numPixels = endPixel - startPixel + 1;
    
    if (numPixels <= 0 || numPixels > cfg.bitmapWidth)
        numPixels = std::min(static_cast<int>(pixelsToShow) + 1, cfg.bitmapWidth);
    
    float srcYTopNorm = (kImageMaxLufs - viewMaxLufs) / kImageLufsRange;
    float srcYBotNorm = (kImageMaxLufs - viewMinLufs) / kImageLufsRange;
    
    int srcY = static_cast<int>(srcYTopNorm * kBitmapHeight);
    int srcH = static_cast<int>((srcYBotNorm - srcYTopNorm) * kBitmapHeight);
    
    srcY = juce::jlimit(0, kBitmapHeight - 1, srcY);
    srcH = juce::jlimit(1, kBitmapHeight - srcY, srcH);
    
    float pixelScale = static_cast<float>(w) / static_cast<float>(numPixels);
    float offsetX = (1.0f - subPixelOffset) * pixelScale;
    
    int newestColumn = (bmp.totalColumnsRendered - 1) % cfg.bitmapWidth;
    int oldestNeededColumn = newestColumn - numPixels + 1;
    
    if (oldestNeededColumn < 0)
        oldestNeededColumn += cfg.bitmapWidth;
    
    int srcStartCol = oldestNeededColumn;
    int srcEndCol = newestColumn;
    
    if (srcStartCol <= srcEndCol)
    {
        int srcW = srcEndCol - srcStartCol + 1;
        int destW = static_cast<int>(srcW * pixelScale + 0.5f);
        int destX = static_cast<int>(offsetX - pixelScale);
        
        g.drawImage(bmp.image,
                    destX, 0, destW + static_cast<int>(pixelScale) + 1, h,
                    srcStartCol, srcY, srcW, srcH);
    }
    else
    {
        int part1SrcW = cfg.bitmapWidth - srcStartCol;
        int part2SrcW = srcEndCol + 1;
        
        float part1DestW = part1SrcW * pixelScale;
        float part2DestW = part2SrcW * pixelScale;
        
        int destX1 = static_cast<int>(offsetX - pixelScale);
        
        g.drawImage(bmp.image,
                    destX1, 0, static_cast<int>(part1DestW) + 1, h,
                    srcStartCol, srcY, part1SrcW, srcH);
        
        g.drawImage(bmp.image,
                    destX1 + static_cast<int>(part1DestW), 0, 
                    static_cast<int>(part2DestW) + 1, h,
                    0, srcY, part2SrcW, srcH);
    }
}

void LoudnessHistoryDisplay::drawGrid(juce::Graphics& g)
{
    int w = getWidth();
    int h = getHeight();
    
    double dataTime = dataStore.getCurrentTime();
    double displayEndTime = dataTime - kDisplayDelay;
    double displayStartTime = displayEndTime - viewTimeRange;
    
    if (displayStartTime < 0.0)
        displayStartTime = 0.0;
    
    float lufsRange = viewMaxLufs - viewMinLufs;
    
    float gridStep = 6.0f;
    if (lufsRange > 40.0f) gridStep = 12.0f;
    if (lufsRange < 20.0f) gridStep = 3.0f;
    
    g.setFont(10.0f);
    
    float startLufs = std::ceil(viewMinLufs / gridStep) * gridStep;
    for (float lufs = startLufs; lufs <= viewMaxLufs; lufs += gridStep)
    {
        float normY = (viewMaxLufs - lufs) / lufsRange;
        int y = static_cast<int>(normY * h);
        
        g.setColour(gridColour);
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(w));
        
        g.setColour(textColour.withAlpha(0.7f));
        g.drawText(juce::String(static_cast<int>(lufs)) + " LUFS",
                   5, y - 12, 60, 12, juce::Justification::left);
    }
    
    double timeStep = 1.0;
    if (viewTimeRange > 30.0) timeStep = 5.0;
    if (viewTimeRange > 60.0) timeStep = 10.0;
    if (viewTimeRange > 300.0) timeStep = 60.0;
    if (viewTimeRange > 900.0) timeStep = 300.0;
    if (viewTimeRange > 3600.0) timeStep = 600.0;
    if (viewTimeRange > 7200.0) timeStep = 1800.0;
    if (viewTimeRange < 5.0) timeStep = 0.5;
    if (viewTimeRange < 2.0) timeStep = 0.25;
    
    double gridStart = std::floor(displayStartTime / timeStep) * timeStep;
    
    for (double t = gridStart; t <= displayEndTime + timeStep; t += timeStep)
    {
        if (t < displayStartTime)
            continue;
        
        float normX = static_cast<float>((t - displayStartTime) / viewTimeRange);
        int x = static_cast<int>(normX * w);
        
        if (x < 0 || x > w)
            continue;
        
        g.setColour(gridColour);
        g.drawVerticalLine(x, 0.0f, static_cast<float>(h));
        
        g.setColour(textColour.withAlpha(0.7f));
        
        juce::String label;
        if (t >= 3600.0)
        {
            int hrs = static_cast<int>(t) / 3600;
            int mins = (static_cast<int>(t) % 3600) / 60;
            int secs = static_cast<int>(t) % 60;
            label = juce::String::formatted("%d:%02d:%02d", hrs, mins, secs);
        }
        else if (t >= 60.0)
        {
            int mins = static_cast<int>(t) / 60;
            int secs = static_cast<int>(t) % 60;
            label = juce::String::formatted("%d:%02d", mins, secs);
        }
        else if (timeStep >= 1.0)
        {
            label = juce::String(static_cast<int>(t)) + "s";
        }
        else
        {
            label = juce::String(t, 1) + "s";
        }
        
        g.drawText(label, x - 30, h - 15, 60, 12, juce::Justification::centred);
    }
}

void LoudnessHistoryDisplay::drawCurrentValues(juce::Graphics& g)
{
    int boxW = 120, boxH = 40, margin = 10;
    
    juce::Rectangle<int> mBox(margin, margin, boxW, boxH);
    g.setColour(momentaryColour.withAlpha(0.85f));
    g.fillRoundedRectangle(mBox.toFloat(), 5.0f);
    g.setColour(juce::Colours::white);
    g.setFont(10.0f);
    g.drawText("Momentary", mBox.removeFromTop(14).reduced(5, 0), juce::Justification::left);
    g.setFont(18.0f);
    juce::String mStr = currentMomentary > -100.0f
        ? juce::String(currentMomentary, 1) + " LUFS"
        : "-inf LUFS";
    g.drawText(mStr, mBox.reduced(5, 0), juce::Justification::left);
    
    juce::Rectangle<int> sBox(margin + boxW + margin, margin, boxW, boxH);
    g.setColour(shortTermColour.withAlpha(0.85f));
    g.fillRoundedRectangle(sBox.toFloat(), 5.0f);
    g.setColour(juce::Colours::white);
    g.setFont(10.0f);
    g.drawText("Short-term", sBox.removeFromTop(14).reduced(5, 0), juce::Justification::left);
    g.setFont(18.0f);
    juce::String sStr = currentShortTerm > -100.0f
        ? juce::String(currentShortTerm, 1) + " LUFS"
        : "-inf LUFS";
    g.drawText(sStr, sBox.reduced(5, 0), juce::Justification::left);
    
    int legendY = getHeight() - 25;
    g.setFont(11.0f);
    
    g.setColour(momentaryColour);
    g.fillRect(margin, legendY, 15, 3);
    g.setColour(textColour);
    g.drawText("Momentary (400ms)", margin + 20, legendY - 6, 120, 15, juce::Justification::left);
    
    g.setColour(shortTermColour);
    g.fillRect(margin + 145, legendY, 15, 3);
    g.setColour(textColour);
    g.drawText("Short-term (3s)", margin + 165, legendY - 6, 100, 15, juce::Justification::left);
}

void LoudnessHistoryDisplay::drawZoomInfo(juce::Graphics& g)
{
    int w = getWidth();
    
    juce::String timeStr;
    if (viewTimeRange >= 3600.0)
        timeStr = juce::String(viewTimeRange / 3600.0, 2) + " hrs";
    else if (viewTimeRange >= 60.0)
        timeStr = juce::String(viewTimeRange / 60.0, 1) + " min";
    else
        timeStr = juce::String(viewTimeRange, 1) + " sec";
    
    float lufsRange = viewMaxLufs - viewMinLufs;
    juce::String lufsStr = juce::String(static_cast<int>(lufsRange)) + " dB";
    
    const auto& cfg = kLodConfigs[static_cast<size_t>(activeLod)];
    juce::String lodStr = "LOD " + juce::String(activeLod);
    juce::String resStr = cfg.secondsPerPixel >= 1.0
        ? juce::String(static_cast<int>(cfg.secondsPerPixel)) + "s/px"
        : juce::String(static_cast<int>(cfg.secondsPerPixel * 1000.0)) + "ms/px";
    
    juce::String info = "X: " + timeStr + " | Y: " + lufsStr + 
                        " | " + lodStr + " (" + resStr + ")";
    
    g.setFont(10.0f);
    g.setColour(textColour.withAlpha(0.6f));
    g.drawText(info, w - 360, 10, 350, 14, juce::Justification::right);
}

void LoudnessHistoryDisplay::resized()
{
}

void LoudnessHistoryDisplay::mouseWheelMove(const juce::MouseEvent& event,
                                             const juce::MouseWheelDetails& wheel)
{
    const float zoomFactor = 1.15f;
    
    if (event.mods.isShiftDown())
    {
        float range = viewMaxLufs - viewMinLufs;
        float mouseRatio = event.position.y / static_cast<float>(getHeight());
        float mouseLufs = viewMaxLufs - mouseRatio * range;
        
        float newRange = (wheel.deltaY > 0) ? range / zoomFactor : range * zoomFactor;
        newRange = juce::jlimit(kMinLufsRange, kMaxLufsRange, newRange);
        
        viewMaxLufs = mouseLufs + mouseRatio * newRange;
        viewMinLufs = viewMaxLufs - newRange;
        
        if (viewMaxLufs > 0.0f)
        {
            viewMaxLufs = 0.0f;
            viewMinLufs = -newRange;
        }
        if (viewMinLufs < kImageMinLufs)
        {
            viewMinLufs = kImageMinLufs;
            viewMaxLufs = kImageMinLufs + newRange;
        }
    }
    else
    {
        double newRange = (wheel.deltaY > 0) ? viewTimeRange / zoomFactor : viewTimeRange * zoomFactor;
        viewTimeRange = juce::jlimit(kMinTimeRange, kMaxTimeRange, newRange);
    }
    
    repaint();
}

void LoudnessHistoryDisplay::mouseDown(const juce::MouseEvent& event)
{
    lastMousePos = event.position;
    isDragging = true;
}

void LoudnessHistoryDisplay::mouseDrag(const juce::MouseEvent& event)
{
    if (!isDragging)
        return;
    
    float dy = event.position.y - lastMousePos.y;
    lastMousePos = event.position;
    
    float lufsRange = viewMaxLufs - viewMinLufs;
    float delta = dy * lufsRange / static_cast<float>(getHeight());
    
    viewMinLufs += delta;
    viewMaxLufs += delta;
    
    if (viewMaxLufs > 0.0f)
    {
        viewMaxLufs = 0.0f;
        viewMinLufs = -lufsRange;
    }
    if (viewMinLufs < kImageMinLufs)
    {
        viewMinLufs = kImageMinLufs;
        viewMaxLufs = kImageMinLufs + lufsRange;
    }
    
    repaint();
}

void LoudnessHistoryDisplay::mouseUp(const juce::MouseEvent&)
{
    isDragging = false;
}