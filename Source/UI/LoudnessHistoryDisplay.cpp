#include "LoudnessHistoryDisplay.h"
#include <cmath>
#include <algorithm>

LoudnessHistoryDisplay::LoudnessHistoryDisplay(LoudnessDataStore& store)
    : dataStore(store)
{
    setOpaque(true);
    startTimerHz(60);
}

LoudnessHistoryDisplay::~LoudnessHistoryDisplay()
{
    stopTimer();
}

void LoudnessHistoryDisplay::timerCallback()
{
    repaint();
}

void LoudnessHistoryDisplay::setCurrentLoudness(float momentary, float shortTerm)
{
    currentMomentary = momentary;
    currentShortTerm = shortTerm;
}

float LoudnessHistoryDisplay::timeToX(double time) const
{
    double normalized = (time - displayStartTime) / (displayEndTime - displayStartTime);
    return static_cast<float>(normalized * getWidth());
}

float LoudnessHistoryDisplay::lufsToY(float lufs) const
{
    float normalized = (viewMaxLufs - lufs) / (viewMaxLufs - viewMinLufs);
    return normalized * static_cast<float>(getHeight());
}

void LoudnessHistoryDisplay::paint(juce::Graphics& g)
{
    // Calculate display time window (right edge is current time minus delay)
    double currentTime = dataStore.getCurrentTime();
    displayEndTime = currentTime - kDisplayDelay;
    displayStartTime = displayEndTime - viewTimeRange;
    
    // Clamp start time to 0
    if (displayStartTime < 0.0)
        displayStartTime = 0.0;
    
    // Query data for current view
    int numBuckets = getWidth();
    if (numBuckets > 0 && displayEndTime > displayStartTime)
    {
        cachedData = dataStore.getMinMaxForRange(displayStartTime, displayEndTime, numBuckets);
    }
    else
    {
        cachedData.clear();
    }
    
    drawBackground(g);
    drawCurves(g);
    drawGrid(g);
    drawCurrentValues(g);
    drawZoomInfo(g);
}

void LoudnessHistoryDisplay::drawBackground(juce::Graphics& g)
{
    g.fillAll(bgColour);
}

void LoudnessHistoryDisplay::drawCurves(juce::Graphics& g)
{
    if (cachedData.empty())
        return;
    
    int w = getWidth();
    int h = getHeight();
    
    if (w <= 0 || h <= 0)
        return;
    
    // Build paths for momentary
    juce::Path mFillPath;
    juce::Path mLinePath;
    
    // Build paths for short-term
    juce::Path sFillPath;
    juce::Path sLinePath;
    
    // Collect valid points
    std::vector<juce::Point<float>> mTopPts;
    std::vector<juce::Point<float>> mBotPts;
    std::vector<juce::Point<float>> mMidPts;
    
    std::vector<juce::Point<float>> sTopPts;
    std::vector<juce::Point<float>> sBotPts;
    std::vector<juce::Point<float>> sMidPts;
    
    float pixelWidth = static_cast<float>(w) / static_cast<float>(cachedData.size());
    
    for (size_t i = 0; i < cachedData.size(); ++i)
    {
        const auto& bucket = cachedData[i];
        float x = static_cast<float>(i) * pixelWidth + pixelWidth * 0.5f;
        
        // Momentary
        if (bucket.momentaryMax > -100.0f)
        {
            float yTop = lufsToY(bucket.momentaryMax);
            float yBot = lufsToY(bucket.momentaryMin);
            float yMid = (yTop + yBot) * 0.5f;
            
            // Clamp to view
            yTop = juce::jlimit(0.0f, static_cast<float>(h), yTop);
            yBot = juce::jlimit(0.0f, static_cast<float>(h), yBot);
            yMid = juce::jlimit(0.0f, static_cast<float>(h), yMid);
            
            mTopPts.push_back({x, yTop});
            mBotPts.push_back({x, yBot});
            mMidPts.push_back({x, yMid});
        }
        
        // Short-term
        if (bucket.shortTermMax > -100.0f)
        {
            float yTop = lufsToY(bucket.shortTermMax);
            float yBot = lufsToY(bucket.shortTermMin);
            float yMid = (yTop + yBot) * 0.5f;
            
            yTop = juce::jlimit(0.0f, static_cast<float>(h), yTop);
            yBot = juce::jlimit(0.0f, static_cast<float>(h), yBot);
            yMid = juce::jlimit(0.0f, static_cast<float>(h), yMid);
            
            sTopPts.push_back({x, yTop});
            sBotPts.push_back({x, yBot});
            sMidPts.push_back({x, yMid});
        }
    }
    
    // Build momentary fill path
    if (mTopPts.size() >= 2)
    {
        mFillPath.startNewSubPath(mTopPts[0]);
        for (size_t i = 1; i < mTopPts.size(); ++i)
        {
            mFillPath.lineTo(mTopPts[i]);
        }
        for (auto it = mBotPts.rbegin(); it != mBotPts.rend(); ++it)
        {
            mFillPath.lineTo(*it);
        }
        mFillPath.closeSubPath();
    }
    
    // Build momentary line path
    if (mMidPts.size() >= 2)
    {
        mLinePath.startNewSubPath(mMidPts[0]);
        for (size_t i = 1; i < mMidPts.size(); ++i)
        {
            mLinePath.lineTo(mMidPts[i]);
        }
    }
    
    // Build short-term fill path
    if (sTopPts.size() >= 2)
    {
        sFillPath.startNewSubPath(sTopPts[0]);
        for (size_t i = 1; i < sTopPts.size(); ++i)
        {
            sFillPath.lineTo(sTopPts[i]);
        }
        for (auto it = sBotPts.rbegin(); it != sBotPts.rend(); ++it)
        {
            sFillPath.lineTo(*it);
        }
        sFillPath.closeSubPath();
    }
    
    // Build short-term line path
    if (sMidPts.size() >= 2)
    {
        sLinePath.startNewSubPath(sMidPts[0]);
        for (size_t i = 1; i < sMidPts.size(); ++i)
        {
            sLinePath.lineTo(sMidPts[i]);
        }
    }
    
    // Draw momentary (behind)
    if (!mFillPath.isEmpty())
    {
        g.setColour(momentaryColour.withAlpha(0.4f));
        g.fillPath(mFillPath);
    }
    if (!mLinePath.isEmpty())
    {
        g.setColour(momentaryColour);
        g.strokePath(mLinePath, juce::PathStrokeType(1.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    // Draw short-term (on top)
    if (!sFillPath.isEmpty())
    {
        g.setColour(shortTermColour.withAlpha(0.5f));
        g.fillPath(sFillPath);
    }
    if (!sLinePath.isEmpty())
    {
        g.setColour(shortTermColour);
        g.strokePath(sLinePath, juce::PathStrokeType(2.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void LoudnessHistoryDisplay::drawGrid(juce::Graphics& g)
{
    int w = getWidth();
    int h = getHeight();
    
    float lufsRange = viewMaxLufs - viewMinLufs;
    
    // Horizontal grid lines (LUFS)
    float gridStep = 6.0f;
    if (lufsRange > 40.0f) gridStep = 12.0f;
    if (lufsRange < 20.0f) gridStep = 3.0f;
    
    g.setFont(10.0f);
    
    float startLufs = std::ceil(viewMinLufs / gridStep) * gridStep;
    for (float lufs = startLufs; lufs <= viewMaxLufs; lufs += gridStep)
    {
        float y = lufsToY(lufs);
        
        g.setColour(gridColour);
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(w));
        
        g.setColour(textColour.withAlpha(0.7f));
        g.drawText(juce::String(static_cast<int>(lufs)) + " LUFS",
                   5, static_cast<int>(y) - 12, 60, 12, juce::Justification::left);
    }
    
    // Vertical grid lines (time)
    double timeRange = displayEndTime - displayStartTime;
    
    double timeStep = 1.0;
    if (timeRange > 30.0) timeStep = 5.0;
    if (timeRange > 60.0) timeStep = 10.0;
    if (timeRange > 300.0) timeStep = 60.0;
    if (timeRange > 900.0) timeStep = 300.0;
    if (timeRange > 3600.0) timeStep = 600.0;
    if (timeRange > 7200.0) timeStep = 1800.0;
    if (timeRange < 5.0) timeStep = 0.5;
    if (timeRange < 2.0) timeStep = 0.25;
    
    double gridStart = std::floor(displayStartTime / timeStep) * timeStep;
    
    for (double t = gridStart; t <= displayEndTime + timeStep; t += timeStep)
    {
        if (t < displayStartTime)
            continue;
        
        float x = timeToX(t);
        
        if (x < 0 || x > w)
            continue;
        
        g.setColour(gridColour);
        g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(h));
        
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
        
        g.drawText(label, static_cast<int>(x) - 30, h - 15, 60, 12, juce::Justification::centred);
    }
}

void LoudnessHistoryDisplay::drawCurrentValues(juce::Graphics& g)
{
    int boxW = 120, boxH = 40, margin = 10;
    
    // Momentary box
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
    
    // Short-term box
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
    
    // Legend
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
    
    double msPerPixel = (viewTimeRange * 1000.0) / getWidth();
    juce::String resStr;
    if (msPerPixel >= 1000.0)
        resStr = juce::String(msPerPixel / 1000.0, 1) + "s/px";
    else
        resStr = juce::String(static_cast<int>(msPerPixel)) + "ms/px";
    
    juce::String info = "X: " + timeStr + " | Y: " + lufsStr + " | " + resStr;
    
    g.setFont(10.0f);
    g.setColour(textColour.withAlpha(0.6f));
    g.drawText(info, w - 300, 10, 290, 14, juce::Justification::right);
}

void LoudnessHistoryDisplay::resized()
{
    // Nothing needed - paths are rebuilt each frame
}

void LoudnessHistoryDisplay::mouseWheelMove(const juce::MouseEvent& event,
                                             const juce::MouseWheelDetails& wheel)
{
    const float zoomFactor = 1.15f;
    
    if (event.mods.isShiftDown())
    {
        // Y-axis zoom
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
        if (viewMinLufs < kAbsoluteMinLufs)
        {
            viewMinLufs = kAbsoluteMinLufs;
            viewMaxLufs = kAbsoluteMinLufs + newRange;
        }
    }
    else
    {
        // X-axis zoom
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
    if (viewMinLufs < kAbsoluteMinLufs)
    {
        viewMinLufs = kAbsoluteMinLufs;
        viewMaxLufs = kAbsoluteMinLufs + lufsRange;
    }
    
    repaint();
}

void LoudnessHistoryDisplay::mouseUp(const juce::MouseEvent&)
{
    isDragging = false;
}