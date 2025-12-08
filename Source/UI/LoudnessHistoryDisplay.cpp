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
    double currentTime = dataStore.getCurrentTime();
    
    // Check if we need to update
    bool needsUpdate = (currentTime != lastDataTime);
    
    if (needsUpdate)
    {
        lastDataTime = currentTime;
        pathsValid = false;
    }
    
    repaint();
}

void LoudnessHistoryDisplay::setCurrentLoudness(float momentary, float shortTerm)
{
    currentMomentary = momentary;
    currentShortTerm = shortTerm;
}

float LoudnessHistoryDisplay::timeToX(double time) const
{
    // displayStartTime maps to 0, displayEndTime maps to width
    // But we DON'T clamp displayStartTime, so negative times map to negative X
    double normalized = (time - displayStartTime) / viewTimeRange;
    return static_cast<float>(normalized * getWidth());
}

float LoudnessHistoryDisplay::lufsToY(float lufs) const
{
    float normalized = (viewMaxLufs - lufs) / (viewMaxLufs - viewMinLufs);
    return normalized * static_cast<float>(getHeight());
}

void LoudnessHistoryDisplay::updateCachedData()
{
    double currentTime = dataStore.getCurrentTime();
    
    // Display window: right edge is currentTime - delay
    displayEndTime = currentTime - kDisplayDelay;
    displayStartTime = displayEndTime - viewTimeRange;
    
    // DON'T clamp displayStartTime to 0 - this keeps scale constant
    // We just won't have data for times < 0
    
    int width = getWidth();
    
    // Check if cache is still valid
    double queryStart = std::max(0.0, displayStartTime);
    double queryEnd = std::max(0.0, displayEndTime);
    
    bool cacheValid = (std::abs(queryStart - lastQueryStartTime) < 0.001 &&
                       std::abs(queryEnd - lastQueryEndTime) < 0.001 &&
                       width == lastQueryWidth &&
                       pathsValid);
    
    if (!cacheValid && width > 0 && queryEnd > queryStart)
    {
        cachedData = dataStore.getDataForTimeRange(queryStart, queryEnd, width);
        lastQueryStartTime = queryStart;
        lastQueryEndTime = queryEnd;
        lastQueryWidth = width;
        pathsValid = false;
    }
}

void LoudnessHistoryDisplay::addCatmullRomSpline(juce::Path& path,
                                                   const std::vector<juce::Point<float>>& points,
                                                   bool startPath)
{
    if (points.size() < 2)
        return;
    
    if (points.size() == 2)
    {
        if (startPath)
            path.startNewSubPath(points[0]);
        path.lineTo(points[1]);
        return;
    }
    
    // Catmull-Rom to Bezier conversion
    // For each segment between points[i] and points[i+1],
    // we need points[i-1] and points[i+2] for tangent calculation
    
    const float tension = 0.5f; // Controls curve tightness (0.5 = Catmull-Rom)
    
    for (size_t i = 0; i < points.size() - 1; ++i)
    {
        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];
        
        // Get neighboring points (with clamping at ends)
        const auto& p0 = (i > 0) ? points[i - 1] : p1;
        const auto& p3 = (i + 2 < points.size()) ? points[i + 2] : p2;
        
        if (i == 0 && startPath)
        {
            path.startNewSubPath(p1);
        }
        
        // Calculate control points for cubic Bezier
        float cp1x = p1.x + (p2.x - p0.x) * tension / 3.0f;
        float cp1y = p1.y + (p2.y - p0.y) * tension / 3.0f;
        float cp2x = p2.x - (p3.x - p1.x) * tension / 3.0f;
        float cp2y = p2.y - (p3.y - p1.y) * tension / 3.0f;
        
        path.cubicTo(cp1x, cp1y, cp2x, cp2y, p2.x, p2.y);
    }
}

void LoudnessHistoryDisplay::buildPaths()
{
    momentaryFillPath.clear();
    momentaryLinePath.clear();
    shortTermFillPath.clear();
    shortTermLinePath.clear();
    
    if (cachedData.points.empty())
    {
        pathsValid = true;
        return;
    }
    
    std::vector<juce::Point<float>> mTopPts;
    std::vector<juce::Point<float>> mBotPts;
    std::vector<juce::Point<float>> mMidPts;
    
    std::vector<juce::Point<float>> sTopPts;
    std::vector<juce::Point<float>> sBotPts;
    std::vector<juce::Point<float>> sMidPts;
    
    float height = static_cast<float>(getHeight());
    
    for (const auto& pt : cachedData.points)
    {
        float x = timeToX(pt.timeMid);
        
        // Skip points outside visible area (with some margin)
        if (x < -50.0f || x > getWidth() + 50.0f)
            continue;
        
        // Momentary
        if (pt.hasValidMomentary())
        {
            float yTop = lufsToY(pt.momentaryMax);
            float yBot = lufsToY(pt.momentaryMin);
            float yMid = (yTop + yBot) * 0.5f;
            
            yTop = juce::jlimit(0.0f, height, yTop);
            yBot = juce::jlimit(0.0f, height, yBot);
            yMid = juce::jlimit(0.0f, height, yMid);
            
            mTopPts.push_back({x, yTop});
            mBotPts.push_back({x, yBot});
            mMidPts.push_back({x, yMid});
        }
        
        // Short-term
        if (pt.hasValidShortTerm())
        {
            float yTop = lufsToY(pt.shortTermMax);
            float yBot = lufsToY(pt.shortTermMin);
            float yMid = (yTop + yBot) * 0.5f;
            
            yTop = juce::jlimit(0.0f, height, yTop);
            yBot = juce::jlimit(0.0f, height, yBot);
            yMid = juce::jlimit(0.0f, height, yMid);
            
            sTopPts.push_back({x, yTop});
            sBotPts.push_back({x, yBot});
            sMidPts.push_back({x, yMid});
        }
    }
    
    // Build momentary fill (top edge forward, bottom edge backward)
    if (mTopPts.size() >= 2)
    {
        addCatmullRomSpline(momentaryFillPath, mTopPts, true);
        
        // Reverse bottom points and add them
        std::vector<juce::Point<float>> mBotReversed(mBotPts.rbegin(), mBotPts.rend());
        addCatmullRomSpline(momentaryFillPath, mBotReversed, false);
        
        momentaryFillPath.closeSubPath();
    }
    
    // Build momentary line
    if (mMidPts.size() >= 2)
    {
        addCatmullRomSpline(momentaryLinePath, mMidPts, true);
    }
    
    // Build short-term fill
    if (sTopPts.size() >= 2)
    {
        addCatmullRomSpline(shortTermFillPath, sTopPts, true);
        
        std::vector<juce::Point<float>> sBotReversed(sBotPts.rbegin(), sBotPts.rend());
        addCatmullRomSpline(shortTermFillPath, sBotReversed, false);
        
        shortTermFillPath.closeSubPath();
    }
    
    // Build short-term line
    if (sMidPts.size() >= 2)
    {
        addCatmullRomSpline(shortTermLinePath, sMidPts, true);
    }
    
    pathsValid = true;
}

void LoudnessHistoryDisplay::paint(juce::Graphics& g)
{
    updateCachedData();
    
    if (!pathsValid)
    {
        buildPaths();
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
    // Draw momentary (behind)
    if (!momentaryFillPath.isEmpty())
    {
        g.setColour(momentaryColour.withAlpha(0.35f));
        g.fillPath(momentaryFillPath);
    }
    if (!momentaryLinePath.isEmpty())
    {
        g.setColour(momentaryColour);
        g.strokePath(momentaryLinePath, juce::PathStrokeType(1.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    // Draw short-term (on top)
    if (!shortTermFillPath.isEmpty())
    {
        g.setColour(shortTermColour.withAlpha(0.45f));
        g.fillPath(shortTermFillPath);
    }
    if (!shortTermLinePath.isEmpty())
    {
        g.setColour(shortTermColour);
        g.strokePath(shortTermLinePath, juce::PathStrokeType(2.0f,
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
    double timeStep = 1.0;
    if (viewTimeRange > 30.0) timeStep = 5.0;
    if (viewTimeRange > 60.0) timeStep = 10.0;
    if (viewTimeRange > 300.0) timeStep = 60.0;
    if (viewTimeRange > 900.0) timeStep = 300.0;
    if (viewTimeRange > 3600.0) timeStep = 600.0;
    if (viewTimeRange > 7200.0) timeStep = 1800.0;
    if (viewTimeRange < 5.0) timeStep = 0.5;
    if (viewTimeRange < 2.0) timeStep = 0.25;
    
    // Start from 0 or displayStartTime, whichever is larger
    double gridStart = std::max(0.0, std::floor(displayStartTime / timeStep) * timeStep);
    
    for (double t = gridStart; t <= displayEndTime + timeStep; t += timeStep)
    {
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
    
    // Time range
    juce::String timeStr;
    if (viewTimeRange >= 3600.0)
        timeStr = juce::String(viewTimeRange / 3600.0, 2) + " hrs";
    else if (viewTimeRange >= 60.0)
        timeStr = juce::String(viewTimeRange / 60.0, 1) + " min";
    else
        timeStr = juce::String(viewTimeRange, 1) + " sec";
    
    // LUFS range
    float lufsRange = viewMaxLufs - viewMinLufs;
    juce::String lufsStr = juce::String(static_cast<int>(lufsRange)) + " dB";
    
    // LOD level
    juce::String lodStr = "LOD " + juce::String(cachedData.lodLevel);
    
    // Bucket duration
    juce::String bucketStr;
    double bucketMs = cachedData.bucketDuration * 1000.0;
    if (bucketMs >= 1000.0)
        bucketStr = juce::String(cachedData.bucketDuration, 1) + "s";
    else
        bucketStr = juce::String(static_cast<int>(bucketMs)) + "ms";
    
    // Points count
    juce::String ptsStr = juce::String(cachedData.points.size()) + " pts";
    
    juce::String info = "X: " + timeStr + " | Y: " + lufsStr + 
                        " | " + lodStr + " (" + bucketStr + ") | " + ptsStr;
    
    g.setFont(10.0f);
    g.setColour(textColour.withAlpha(0.6f));
    g.drawText(info, w - 380, 10, 370, 14, juce::Justification::right);
}

void LoudnessHistoryDisplay::resized()
{
    pathsValid = false;
    lastQueryWidth = 0;
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
    
    pathsValid = false;
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
    
    pathsValid = false;
    repaint();
}

void LoudnessHistoryDisplay::mouseUp(const juce::MouseEvent&)
{
    isDragging = false;
}