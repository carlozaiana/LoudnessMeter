#include "LoudnessHistoryDisplay.h"
#include <cmath>

namespace
{
    constexpr float kFloatEpsilon = 0.0001f;
    constexpr double kDoubleEpsilon = 0.0001;
    
    bool floatEqual(float a, float b)
    {
        return std::abs(a - b) < kFloatEpsilon;
    }
    
    bool doubleEqual(double a, double b)
    {
        return std::abs(a - b) < kDoubleEpsilon;
    }
}

LoudnessHistoryDisplay::LoudnessHistoryDisplay(LoudnessDataStore& store)
    : dataStore(store)
{
    setOpaque(true);
    lastTimerTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz(20);
}

LoudnessHistoryDisplay::~LoudnessHistoryDisplay()
{
    stopTimer();
}

void LoudnessHistoryDisplay::timerCallback()
{
    double currentTimeSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    double deltaTime = currentTimeSeconds - lastTimerTime;
    lastTimerTime = currentTimeSeconds;
    
    // Handle zoom cooldown
    if (isZooming)
    {
        zoomCooldownTime -= deltaTime;
        if (zoomCooldownTime <= 0.0)
        {
            isZooming = false;
            zoomCooldownTime = 0.0;
        }
    }
    
    double dataCurrentTime = dataStore.getCurrentTime();
    
    // Auto-scroll only when NOT zooming and NOT dragging
    if (!isZooming && !isDragging)
    {
        double targetStart = dataCurrentTime - viewTimeRange * 0.9;
        double diff = targetStart - viewStartTime;
        
        if (std::abs(diff) > 0.001)
        {
            viewStartTime += diff * 0.12;
            pathsNeedRebuild = true;
        }
    }
    
    // Check if data has changed
    if (!doubleEqual(dataCurrentTime, lastDataTime))
    {
        lastDataTime = dataCurrentTime;
        pathsNeedRebuild = true;
    }
    
    if (pathsNeedRebuild)
    {
        repaint();
    }
}

void LoudnessHistoryDisplay::setCurrentLoudness(float momentary, float shortTerm)
{
    currentMomentary = momentary;
    currentShortTerm = shortTerm;
}

void LoudnessHistoryDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;
    
    // Check if we need to rebuild paths
    bool viewChanged = (!doubleEqual(viewStartTime, lastViewStartTime) ||
                        !doubleEqual(viewTimeRange, lastViewTimeRange) ||
                        !floatEqual(viewMinLufs, lastViewMinLufs) ||
                        !floatEqual(viewMaxLufs, lastViewMaxLufs) ||
                        getWidth() != lastWidth ||
                        getHeight() != lastHeight);
    
    if (pathsNeedRebuild || viewChanged)
    {
        rebuildPaths();
        lastViewStartTime = viewStartTime;
        lastViewTimeRange = viewTimeRange;
        lastViewMinLufs = viewMinLufs;
        lastViewMaxLufs = viewMaxLufs;
        lastWidth = getWidth();
        lastHeight = getHeight();
        pathsNeedRebuild = false;
    }
    
    drawBackground(g);
    drawGrid(g);
    drawCurves(g);
    drawCurrentValues(g);
    drawZoomInfo(g);
}

void LoudnessHistoryDisplay::rebuildPaths()
{
    int width = getWidth();
    int height = getHeight();
    
    if (width <= 0 || height <= 0)
        return;
    
    double endTime = viewStartTime + viewTimeRange;
    cachedRenderData = dataStore.getDataForTimeRange(viewStartTime, endTime, width);
    
    // Clear all paths
    momentaryLinePath.clear();
    shortTermLinePath.clear();
    momentaryFillPath.clear();
    shortTermFillPath.clear();
    
    if (cachedRenderData.points.empty())
        return;
    
    const auto& points = cachedRenderData.points;
    
    // Build paths using the center of each time bucket for X position
    // This ensures consistent positioning regardless of scroll position
    
    // Momentary fill path (envelope)
    momentaryFillPath.startNewSubPath(
        timeToX(points.front().startTime),
        loudnessToY(points.front().maxMomentary)
    );
    
    // Top edge (max values)
    for (const auto& p : points)
    {
        float xStart = timeToX(p.startTime);
        float xEnd = timeToX(p.endTime);
        float yMax = loudnessToY(p.maxMomentary);
        
        // Draw horizontal line for this bucket's max
        momentaryFillPath.lineTo(xStart, yMax);
        momentaryFillPath.lineTo(xEnd, yMax);
    }
    
    // Bottom edge (min values) - reverse order
    for (auto it = points.rbegin(); it != points.rend(); ++it)
    {
        float xStart = timeToX(it->startTime);
        float xEnd = timeToX(it->endTime);
        float yMin = loudnessToY(it->minMomentary);
        
        momentaryFillPath.lineTo(xEnd, yMin);
        momentaryFillPath.lineTo(xStart, yMin);
    }
    
    momentaryFillPath.closeSubPath();
    
    // Short-term fill path (envelope)
    shortTermFillPath.startNewSubPath(
        timeToX(points.front().startTime),
        loudnessToY(points.front().maxShortTerm)
    );
    
    // Top edge (max values)
    for (const auto& p : points)
    {
        float xStart = timeToX(p.startTime);
        float xEnd = timeToX(p.endTime);
        float yMax = loudnessToY(p.maxShortTerm);
        
        shortTermFillPath.lineTo(xStart, yMax);
        shortTermFillPath.lineTo(xEnd, yMax);
    }
    
    // Bottom edge (min values) - reverse order
    for (auto it = points.rbegin(); it != points.rend(); ++it)
    {
        float xStart = timeToX(it->startTime);
        float xEnd = timeToX(it->endTime);
        float yMin = loudnessToY(it->minShortTerm);
        
        shortTermFillPath.lineTo(xEnd, yMin);
        shortTermFillPath.lineTo(xStart, yMin);
    }
    
    shortTermFillPath.closeSubPath();
    
    // Build center line paths
    bool first = true;
    for (const auto& p : points)
    {
        float xMid = timeToX((p.startTime + p.endTime) * 0.5);
        float yM = loudnessToY((p.minMomentary + p.maxMomentary) * 0.5f);
        float yS = loudnessToY((p.minShortTerm + p.maxShortTerm) * 0.5f);
        
        if (first)
        {
            momentaryLinePath.startNewSubPath(xMid, yM);
            shortTermLinePath.startNewSubPath(xMid, yS);
            first = false;
        }
        else
        {
            momentaryLinePath.lineTo(xMid, yM);
            shortTermLinePath.lineTo(xMid, yS);
        }
    }
}

void LoudnessHistoryDisplay::resized()
{
    pathsNeedRebuild = true;
}

void LoudnessHistoryDisplay::mouseWheelMove(const juce::MouseEvent& event,
                                             const juce::MouseWheelDetails& wheel)
{
    // Activate zoom mode and reset cooldown
    isZooming = true;
    zoomCooldownTime = kZoomCooldownDuration;
    
    const float zoomFactor = 1.15f;
    
    if (event.mods.isShiftDown())
    {
        // Y-axis zoom
        float range = viewMaxLufs - viewMinLufs;
        float mouseRatio = event.position.y / static_cast<float>(getHeight());
        float mouseLufs = viewMaxLufs - mouseRatio * range;
        
        float newRange = range;
        if (wheel.deltaY > 0)
            newRange = range / zoomFactor;
        else if (wheel.deltaY < 0)
            newRange = range * zoomFactor;
        
        newRange = juce::jlimit(kMinLufsRange, kMaxLufsRange, newRange);
        
        viewMaxLufs = mouseLufs + mouseRatio * newRange;
        viewMinLufs = viewMaxLufs - newRange;
        
        if (viewMaxLufs > 0.0f)
        {
            viewMaxLufs = 0.0f;
            viewMinLufs = -newRange;
        }
        if (viewMinLufs < -100.0f)
        {
            viewMinLufs = -100.0f;
            viewMaxLufs = viewMinLufs + newRange;
        }
    }
    else
    {
        // X-axis zoom
        double mouseRatio = static_cast<double>(event.position.x) / static_cast<double>(getWidth());
        double mouseTime = viewStartTime + mouseRatio * viewTimeRange;
        
        double newRange = viewTimeRange;
        if (wheel.deltaY > 0)
            newRange = viewTimeRange / zoomFactor;
        else if (wheel.deltaY < 0)
            newRange = viewTimeRange * zoomFactor;
        
        newRange = juce::jlimit(kMinTimeRange, kMaxTimeRange, newRange);
        
        viewTimeRange = newRange;
        viewStartTime = mouseTime - mouseRatio * newRange;
    }
    
    pathsNeedRebuild = true;
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
    
    float dx = event.position.x - lastMousePos.x;
    float dy = event.position.y - lastMousePos.y;
    lastMousePos = event.position;
    
    double timeDelta = static_cast<double>(-dx) * viewTimeRange / static_cast<double>(getWidth());
    viewStartTime += timeDelta;
    
    float lufsRange = viewMaxLufs - viewMinLufs;
    float lufsDelta = dy * lufsRange / static_cast<float>(getHeight());
    viewMinLufs += lufsDelta;
    viewMaxLufs += lufsDelta;
    
    pathsNeedRebuild = true;
    repaint();
}

void LoudnessHistoryDisplay::mouseUp(const juce::MouseEvent&)
{
    isDragging = false;
}

void LoudnessHistoryDisplay::drawBackground(juce::Graphics& g)
{
    g.fillAll(backgroundColour);
}

void LoudnessHistoryDisplay::drawGrid(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    float width = static_cast<float>(bounds.getWidth());
    float height = static_cast<float>(bounds.getHeight());
    
    // Horizontal grid lines (LUFS levels)
    float lufsRange = viewMaxLufs - viewMinLufs;
    float gridStep = 6.0f;
    if (lufsRange > 40.0f) gridStep = 12.0f;
    if (lufsRange < 20.0f) gridStep = 3.0f;
    
    float startLufs = std::ceil(viewMinLufs / gridStep) * gridStep;
    
    g.setFont(10.0f);
    for (float lufs = startLufs; lufs <= viewMaxLufs; lufs += gridStep)
    {
        float y = loudnessToY(lufs);
        
        // Don't snap - allow sub-pixel anti-aliased rendering for smooth scrolling
        g.setColour(gridColour);
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, width);
        
        g.setColour(textColour.withAlpha(0.7f));
        g.drawText(juce::String(static_cast<int>(lufs)) + " LUFS",
                   5, static_cast<int>(y) - 12, 60, 12,
                   juce::Justification::left);
    }
    
    // Vertical grid lines (time) - draw at fixed time intervals
    double timeStep = 1.0;
    if (viewTimeRange > 30.0) timeStep = 5.0;
    if (viewTimeRange > 60.0) timeStep = 10.0;
    if (viewTimeRange > 300.0) timeStep = 60.0;
    if (viewTimeRange > 900.0) timeStep = 300.0;
    if (viewTimeRange > 3600.0) timeStep = 600.0;
    if (viewTimeRange > 7200.0) timeStep = 1800.0;
    if (viewTimeRange < 5.0) timeStep = 0.5;
    if (viewTimeRange < 2.0) timeStep = 0.25;
    
    // Align grid to fixed time values
    double gridStartTime = std::floor(viewStartTime / timeStep) * timeStep;
    
    for (double t = gridStartTime; t < viewStartTime + viewTimeRange + timeStep; t += timeStep)
    {
        if (t < viewStartTime) continue;
        
        float x = timeToX(t);
        
        if (x < 0.0f || x > width) continue;
        
        // Draw at sub-pixel position for smooth scrolling
        g.setColour(gridColour);
        g.drawVerticalLine(static_cast<int>(x), 0.0f, height);
        
        g.setColour(textColour.withAlpha(0.7f));
        
        juce::String timeLabel;
        double absT = std::abs(t);
        if (absT >= 3600.0)
        {
            int hours = static_cast<int>(absT) / 3600;
            int minutes = (static_cast<int>(absT) % 3600) / 60;
            int seconds = static_cast<int>(absT) % 60;
            timeLabel = juce::String::formatted("%d:%02d:%02d", hours, minutes, seconds);
        }
        else if (absT >= 60.0)
        {
            int minutes = static_cast<int>(absT) / 60;
            int seconds = static_cast<int>(absT) % 60;
            timeLabel = juce::String::formatted("%d:%02d", minutes, seconds);
        }
        else if (timeStep >= 1.0)
        {
            timeLabel = juce::String(static_cast<int>(t)) + "s";
        }
        else
        {
            timeLabel = juce::String(t, 1) + "s";
        }
        
        if (t < 0) timeLabel = "-" + timeLabel;
        
        g.drawText(timeLabel, static_cast<int>(x) - 30, static_cast<int>(height) - 15, 60, 12,
                   juce::Justification::centred);
    }
}

void LoudnessHistoryDisplay::drawCurves(juce::Graphics& g)
{
    // Draw Momentary FIRST (behind)
    if (!momentaryFillPath.isEmpty())
    {
        g.setColour(momentaryColour.withAlpha(0.25f));
        g.fillPath(momentaryFillPath);
    }
    
    if (!momentaryLinePath.isEmpty())
    {
        g.setColour(momentaryColour.withAlpha(0.8f));
        g.strokePath(momentaryLinePath, juce::PathStrokeType(1.0f, 
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    // Draw Short-term SECOND (on top)
    if (!shortTermFillPath.isEmpty())
    {
        g.setColour(shortTermColour.withAlpha(0.35f));
        g.fillPath(shortTermFillPath);
    }
    
    if (!shortTermLinePath.isEmpty())
    {
        g.setColour(shortTermColour);
        g.strokePath(shortTermLinePath, juce::PathStrokeType(1.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void LoudnessHistoryDisplay::drawCurrentValues(juce::Graphics& g)
{
    int boxWidth = 120;
    int boxHeight = 40;
    int margin = 10;
    
    // Momentary box
    juce::Rectangle<int> momentaryBox(margin, margin, boxWidth, boxHeight);
    g.setColour(momentaryColour.withAlpha(0.85f));
    g.fillRoundedRectangle(momentaryBox.toFloat(), 5.0f);
    g.setColour(juce::Colours::white);
    g.setFont(10.0f);
    g.drawText("Momentary", momentaryBox.removeFromTop(14).reduced(5, 0), 
               juce::Justification::left);
    g.setFont(18.0f);
    
    juce::String momentaryStr;
    if (currentMomentary > -100.0f)
        momentaryStr = juce::String(currentMomentary, 1) + " LUFS";
    else
        momentaryStr = "-inf LUFS";
    g.drawText(momentaryStr, momentaryBox.reduced(5, 0), 
               juce::Justification::left);
    
    // Short-term box
    juce::Rectangle<int> shortTermBox(margin + boxWidth + margin, margin, boxWidth, boxHeight);
    g.setColour(shortTermColour.withAlpha(0.85f));
    g.fillRoundedRectangle(shortTermBox.toFloat(), 5.0f);
    g.setColour(juce::Colours::white);
    g.setFont(10.0f);
    g.drawText("Short-term", shortTermBox.removeFromTop(14).reduced(5, 0), 
               juce::Justification::left);
    g.setFont(18.0f);
    
    juce::String shortTermStr;
    if (currentShortTerm > -100.0f)
        shortTermStr = juce::String(currentShortTerm, 1) + " LUFS";
    else
        shortTermStr = "-inf LUFS";
    g.drawText(shortTermStr, shortTermBox.reduced(5, 0), 
               juce::Justification::left);
    
    // Legend at bottom
    int legendY = getHeight() - 25;
    g.setFont(11.0f);
    
    g.setColour(momentaryColour);
    g.fillRect(margin, legendY, 15, 3);
    g.setColour(textColour);
    g.drawText("Momentary (400ms)", margin + 20, legendY - 6, 120, 15,
               juce::Justification::left);
    
    g.setColour(shortTermColour);
    g.fillRect(margin + 145, legendY, 15, 3);
    g.setColour(textColour);
    g.drawText("Short-term (3s)", margin + 165, legendY - 6, 100, 15,
               juce::Justification::left);
}

void LoudnessHistoryDisplay::drawZoomInfo(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    
    // Format time range
    juce::String timeStr;
    if (viewTimeRange >= 3600.0)
    {
        timeStr = juce::String(viewTimeRange / 3600.0, 2) + " hrs";
    }
    else if (viewTimeRange >= 60.0)
    {
        timeStr = juce::String(viewTimeRange / 60.0, 1) + " min";
    }
    else
    {
        timeStr = juce::String(viewTimeRange, 1) + " sec";
    }
    
    // Format LUFS range
    float lufsRange = viewMaxLufs - viewMinLufs;
    juce::String lufsStr = juce::String(static_cast<int>(lufsRange)) + " dB";
    
    // LOD info
    juce::String lodStr = "LOD " + juce::String(cachedRenderData.lodLevel);
    
    // Bucket duration
    juce::String bucketStr;
    double bucketMs = cachedRenderData.bucketDuration * 1000.0;
    if (bucketMs >= 1000.0)
    {
        bucketStr = juce::String(cachedRenderData.bucketDuration, 1) + "s";
    }
    else
    {
        bucketStr = juce::String(static_cast<int>(bucketMs)) + "ms";
    }
    
    // Points count
    juce::String pointsStr = juce::String(cachedRenderData.points.size()) + " pts";
    
    // Zoom/scroll status
    juce::String statusStr;
    if (isZooming)
        statusStr = "[ZOOM]";
    else if (isDragging)
        statusStr = "[DRAG]";
    else
        statusStr = "[AUTO]";
    
    juce::String infoStr = "X: " + timeStr + " | Y: " + lufsStr + " | " + 
                           lodStr + " (" + bucketStr + ") | " + pointsStr + " " + statusStr;
    
    g.setFont(10.0f);
    g.setColour(textColour.withAlpha(0.6f));
    
    int textWidth = 400;
    g.drawText(infoStr, bounds.getWidth() - textWidth - 10, 10, textWidth, 14,
               juce::Justification::right);
}

float LoudnessHistoryDisplay::timeToX(double time) const
{
    return static_cast<float>((time - viewStartTime) / viewTimeRange * static_cast<double>(getWidth()));
}

float LoudnessHistoryDisplay::loudnessToY(float lufs) const
{
    float range = viewMaxLufs - viewMinLufs;
    if (range <= 0.0f) return 0.0f;
    return (viewMaxLufs - lufs) / range * static_cast<float>(getHeight());
}