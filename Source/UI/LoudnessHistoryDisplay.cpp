#include "LoudnessHistoryDisplay.h"
#include <cmath>
#include <algorithm>

LoudnessHistoryDisplay::LoudnessHistoryDisplay(LoudnessDataStore& store)
    : dataStore(store)
{
    setOpaque(true);
    startTimerHz(30); // 30 Hz is enough for smooth display
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
    double normalized = (time - displayStartTime) / viewTimeRange;
    return static_cast<float>(normalized * getWidth());
}

float LoudnessHistoryDisplay::lufsToY(float lufs) const
{
    float normalized = (viewMaxLufs - lufs) / (viewMaxLufs - viewMinLufs);
    return normalized * static_cast<float>(getHeight());
}

bool LoudnessHistoryDisplay::updateCachedDataIfNeeded()
{
    double currentTime = dataStore.getCurrentTime();
    
    displayEndTime = currentTime - kDisplayDelay;
    displayStartTime = displayEndTime - viewTimeRange;
    
    int width = getWidth();
    
    // Check if cache is still valid
    // Allow small time drift without re-querying (scrolling)
    double timeDrift = std::abs(displayEndTime - cacheDisplayEnd);
    bool significantDrift = timeDrift > (viewTimeRange * 0.1); // 10% drift triggers update
    
    bool viewChanged = (std::abs(viewTimeRange - cacheTimeRange) > 0.001 ||
                        width != cacheWidth);
    
    // Check if new data arrived (more points in data store)
    bool newData = false;
    {
        double queryStart = std::max(0.0, displayStartTime);
        double queryEnd = std::max(0.0, displayEndTime);
        
        if (queryEnd > queryStart && width > 0)
        {
            // Quick check: just see if current bucket changed
            auto tempResult = dataStore.getDataForDisplay(queryStart, queryEnd, kTargetPointsPerWidth);
            newData = (tempResult.points.size() != cacheDataSize);
            
            if (newData || viewChanged || significantDrift)
            {
                cachedData = std::move(tempResult);
                cacheDisplayStart = displayStartTime;
                cacheDisplayEnd = displayEndTime;
                cacheTimeRange = viewTimeRange;
                cacheWidth = width;
                cacheDataSize = cachedData.points.size();
                pathsNeedRebuild = true;
                return true;
            }
        }
    }
    
    // Update display times for rendering even if cache is valid
    cacheDisplayStart = displayStartTime;
    cacheDisplayEnd = displayEndTime;
    
    return false;
}

void LoudnessHistoryDisplay::buildSmoothPath(juce::Path& path, 
                                              const std::vector<juce::Point<float>>& points)
{
    if (points.size() < 2)
    {
        if (points.size() == 1)
        {
            path.startNewSubPath(points[0]);
            path.lineTo(points[0].x + 0.1f, points[0].y);
        }
        return;
    }
    
    path.startNewSubPath(points[0]);
    
    if (points.size() == 2)
    {
        path.lineTo(points[1]);
        return;
    }
    
    // Catmull-Rom spline converted to cubic Bezier
    const float tension = 0.5f;
    
    for (size_t i = 0; i < points.size() - 1; ++i)
    {
        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];
        
        const auto& p0 = (i > 0) ? points[i - 1] : p1;
        const auto& p3 = (i + 2 < points.size()) ? points[i + 2] : p2;
        
        float cp1x = p1.x + (p2.x - p0.x) * tension / 3.0f;
        float cp1y = p1.y + (p2.y - p0.y) * tension / 3.0f;
        float cp2x = p2.x - (p3.x - p1.x) * tension / 3.0f;
        float cp2y = p2.y - (p3.y - p1.y) * tension / 3.0f;
        
        path.cubicTo(cp1x, cp1y, cp2x, cp2y, p2.x, p2.y);
    }
}

void LoudnessHistoryDisplay::buildFillPath(juce::Path& path,
                                            const std::vector<juce::Point<float>>& topPoints,
                                            const std::vector<juce::Point<float>>& bottomPoints)
{
    if (topPoints.size() < 2 || bottomPoints.size() < 2)
        return;
    
    // Build top edge
    buildSmoothPath(path, topPoints);
    
    // Build bottom edge in reverse (continuing the path)
    std::vector<juce::Point<float>> reversedBottom(bottomPoints.rbegin(), bottomPoints.rend());
    
    const float tension = 0.5f;
    
    // Connect to first point of reversed bottom
    path.lineTo(reversedBottom[0]);
    
    // Continue with smooth curve for bottom
    for (size_t i = 0; i < reversedBottom.size() - 1; ++i)
    {
        const auto& p1 = reversedBottom[i];
        const auto& p2 = reversedBottom[i + 1];
        
        const auto& p0 = (i > 0) ? reversedBottom[i - 1] : p1;
        const auto& p3 = (i + 2 < reversedBottom.size()) ? reversedBottom[i + 2] : p2;
        
        float cp1x = p1.x + (p2.x - p0.x) * tension / 3.0f;
        float cp1y = p1.y + (p2.y - p0.y) * tension / 3.0f;
        float cp2x = p2.x - (p3.x - p1.x) * tension / 3.0f;
        float cp2y = p2.y - (p3.y - p1.y) * tension / 3.0f;
        
        path.cubicTo(cp1x, cp1y, cp2x, cp2y, p2.x, p2.y);
    }
    
    path.closeSubPath();
}

void LoudnessHistoryDisplay::buildPaths()
{
    momentaryFillPath.clear();
    momentaryLinePath.clear();
    shortTermFillPath.clear();
    shortTermLinePath.clear();
    
    if (cachedData.points.empty())
    {
        pathsNeedRebuild = false;
        return;
    }
    
    std::vector<juce::Point<float>> mTopPts;
    std::vector<juce::Point<float>> mBotPts;
    std::vector<juce::Point<float>> mMidPts;
    
    std::vector<juce::Point<float>> sTopPts;
    std::vector<juce::Point<float>> sBotPts;
    std::vector<juce::Point<float>> sMidPts;
    
    float height = static_cast<float>(getHeight());
    float width = static_cast<float>(getWidth());
    
    for (const auto& pt : cachedData.points)
    {
        float x = timeToX(pt.timeMid);
        
        // Skip points well outside visible area
        if (x < -100.0f || x > width + 100.0f)
            continue;
        
        // Momentary
        if (pt.hasValidMomentary())
        {
            float yTop = lufsToY(pt.momentaryMax);
            float yBot = lufsToY(pt.momentaryMin);
            float yMid = (yTop + yBot) * 0.5f;
            
            yTop = juce::jlimit(-10.0f, height + 10.0f, yTop);
            yBot = juce::jlimit(-10.0f, height + 10.0f, yBot);
            yMid = juce::jlimit(-10.0f, height + 10.0f, yMid);
            
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
            
            yTop = juce::jlimit(-10.0f, height + 10.0f, yTop);
            yBot = juce::jlimit(-10.0f, height + 10.0f, yBot);
            yMid = juce::jlimit(-10.0f, height + 10.0f, yMid);
            
            sTopPts.push_back({x, yTop});
            sBotPts.push_back({x, yBot});
            sMidPts.push_back({x, yMid});
        }
    }
    
    // Build paths
    if (mTopPts.size() >= 2)
    {
        buildFillPath(momentaryFillPath, mTopPts, mBotPts);
        buildSmoothPath(momentaryLinePath, mMidPts);
    }
    
    if (sTopPts.size() >= 2)
    {
        buildFillPath(shortTermFillPath, sTopPts, sBotPts);
        buildSmoothPath(shortTermLinePath, sMidPts);
    }
    
    pathsNeedRebuild = false;
}

void LoudnessHistoryDisplay::paint(juce::Graphics& g)
{
    updateCachedDataIfNeeded();
    
    if (pathsNeedRebuild)
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
    
    // Horizontal grid (LUFS)
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
    
    // Vertical grid (time)
    double timeStep = 1.0;
    if (viewTimeRange > 30.0) timeStep = 5.0;
    if (viewTimeRange > 60.0) timeStep = 10.0;
    if (viewTimeRange > 300.0) timeStep = 60.0;
    if (viewTimeRange > 900.0) timeStep = 300.0;
    if (viewTimeRange > 3600.0) timeStep = 600.0;
    if (viewTimeRange > 7200.0) timeStep = 1800.0;
    if (viewTimeRange < 5.0) timeStep = 0.5;
    if (viewTimeRange < 2.0) timeStep = 0.25;
    
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
    
    // Momentary
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
    
    // Short-term
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
    
    juce::String lodStr = "LOD " + juce::String(cachedData.lodLevel);
    
    juce::String bucketStr;
    double bucketMs = cachedData.bucketDuration * 1000.0;
    if (bucketMs >= 1000.0)
        bucketStr = juce::String(cachedData.bucketDuration, 1) + "s";
    else
        bucketStr = juce::String(static_cast<int>(bucketMs)) + "ms";
    
    juce::String ptsStr = juce::String(cachedData.points.size()) + " pts";
    
    juce::String info = "X: " + timeStr + " | Y: " + lufsStr + 
                        " | " + lodStr + " (" + bucketStr + ") | " + ptsStr;
    
    g.setFont(10.0f);
    g.setColour(textColour.withAlpha(0.6f));
    g.drawText(info, w - 380, 10, 370, 14, juce::Justification::right);
}

void LoudnessHistoryDisplay::resized()
{
    pathsNeedRebuild = true;
    cacheWidth = 0; // Force cache invalidation
}

void LoudnessHistoryDisplay::mouseWheelMove(const juce::MouseEvent& event,
                                             const juce::MouseWheelDetails& wheel)
{
    const float zoomFactor = 1.15f;
    
    if (event.mods.isShiftDown())
    {
        // Y zoom
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
        // X zoom
        double newRange = (wheel.deltaY > 0) ? viewTimeRange / zoomFactor : viewTimeRange * zoomFactor;
        viewTimeRange = juce::jlimit(kMinTimeRange, kMaxTimeRange, newRange);
    }
    
    pathsNeedRebuild = true;
    cacheTimeRange = -1.0; // Force cache invalidation
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
    
    pathsNeedRebuild = true;
    repaint();
}

void LoudnessHistoryDisplay::mouseUp(const juce::MouseEvent&)
{
    isDragging = false;
}