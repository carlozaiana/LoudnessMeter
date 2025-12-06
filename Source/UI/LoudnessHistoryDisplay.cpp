#include "LoudnessHistoryDisplay.h"
#include <cmath>

LoudnessHistoryDisplay::LoudnessHistoryDisplay(LoudnessDataStore& store)
    : dataStore(store)
{
    setOpaque(true);
    startTimerHz(20); // 20 Hz for smooth scrolling without excessive CPU
}

LoudnessHistoryDisplay::~LoudnessHistoryDisplay()
{
    stopTimer();
}

void LoudnessHistoryDisplay::timerCallback()
{
    double currentTime = dataStore.getCurrentTime();
    
    // Auto-scroll to follow current time
    double targetStart = currentTime - viewTimeRange * 0.9;
    double diff = targetStart - viewStartTime;
    
    // Smooth scrolling
    if (std::abs(diff) > 0.001)
    {
        viewStartTime += diff * 0.15;
        pathsNeedRebuild = true;
    }
    
    // Check if data has changed
    if (currentTime != lastDataTime)
    {
        lastDataTime = currentTime;
        pathsNeedRebuild = true;
    }
    
    if (pathsNeedRebuild)
    {
        repaint();
    }
}

void LoudnessHistoryDisplay::setCurrentLoudness(float momentary, float shortTerm)
{
    if (momentary != currentMomentary || shortTerm != currentShortTerm)
    {
        currentMomentary = momentary;
        currentShortTerm = shortTerm;
        // Don't set pathsNeedRebuild - this is just the current value display
    }
}

void LoudnessHistoryDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;
    
    // Check if we need to rebuild paths
    if (pathsNeedRebuild || 
        viewStartTime != lastViewStartTime ||
        viewTimeRange != lastViewTimeRange ||
        getWidth() != lastWidth ||
        getHeight() != lastHeight)
    {
        rebuildPaths();
        lastViewStartTime = viewStartTime;
        lastViewTimeRange = viewTimeRange;
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
    
    // Clear paths
    momentaryPath.clear();
    shortTermPath.clear();
    momentaryFillPath.clear();
    shortTermFillPath.clear();
    
    if (cachedRenderData.useMinMax && !cachedRenderData.minMaxPoints.empty())
    {
        // Build fill paths for min/max envelope
        const auto& points = cachedRenderData.minMaxPoints;
        
        // Momentary envelope
        bool first = true;
        for (const auto& p : points)
        {
            float x = timeToX(p.timestamp);
            float yMax = loudnessToY(p.maxMomentary);
            
            if (first)
            {
                momentaryFillPath.startNewSubPath(x, yMax);
                first = false;
            }
            else
            {
                momentaryFillPath.lineTo(x, yMax);
            }
        }
        // Return along min values
        for (auto it = points.rbegin(); it != points.rend(); ++it)
        {
            float x = timeToX(it->timestamp);
            float yMin = loudnessToY(it->minMomentary);
            momentaryFillPath.lineTo(x, yMin);
        }
        momentaryFillPath.closeSubPath();
        
        // Short-term envelope
        first = true;
        for (const auto& p : points)
        {
            float x = timeToX(p.timestamp);
            float yMax = loudnessToY(p.maxShortTerm);
            
            if (first)
            {
                shortTermFillPath.startNewSubPath(x, yMax);
                first = false;
            }
            else
            {
                shortTermFillPath.lineTo(x, yMax);
            }
        }
        for (auto it = points.rbegin(); it != points.rend(); ++it)
        {
            float x = timeToX(it->timestamp);
            float yMin = loudnessToY(it->minShortTerm);
            shortTermFillPath.lineTo(x, yMin);
        }
        shortTermFillPath.closeSubPath();
        
        // Also build center line paths
        first = true;
        for (const auto& p : points)
        {
            float x = timeToX(p.timestamp);
            float yM = loudnessToY((p.minMomentary + p.maxMomentary) * 0.5f);
            float yS = loudnessToY((p.minShortTerm + p.maxShortTerm) * 0.5f);
            
            if (first)
            {
                momentaryPath.startNewSubPath(x, yM);
                shortTermPath.startNewSubPath(x, yS);
                first = false;
            }
            else
            {
                momentaryPath.lineTo(x, yM);
                shortTermPath.lineTo(x, yS);
            }
        }
    }
    else if (!cachedRenderData.points.empty())
    {
        // Build simple line paths
        bool first = true;
        for (const auto& p : cachedRenderData.points)
        {
            float x = timeToX(p.timestamp);
            float yM = loudnessToY(p.momentary);
            float yS = loudnessToY(p.shortTerm);
            
            if (first)
            {
                momentaryPath.startNewSubPath(x, yM);
                shortTermPath.startNewSubPath(x, yS);
                first = false;
            }
            else
            {
                momentaryPath.lineTo(x, yM);
                shortTermPath.lineTo(x, yS);
            }
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
    int width = bounds.getWidth();
    int height = bounds.getHeight();
    
    // Horizontal grid lines (LUFS levels)
    float lufsRange = viewMaxLufs - viewMinLufs;
    float gridStep = 6.0f;
    if (lufsRange > 40.0f) gridStep = 12.0f;
    if (lufsRange < 20.0f) gridStep = 3.0f;
    
    float startLufs = std::ceil(viewMinLufs / gridStep) * gridStep;
    
    g.setFont(10.0f);
    for (float lufs = startLufs; lufs <= viewMaxLufs; lufs += gridStep)
    {
        int y = snapToPixel(loudnessToY(lufs));
        
        g.setColour(gridColour);
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(width));
        
        g.setColour(textColour.withAlpha(0.7f));
        g.drawText(juce::String(static_cast<int>(lufs)) + " LUFS",
                   5, y - 12, 60, 12,
                   juce::Justification::left);
    }
    
    // Vertical grid lines (time)
    double timeStep = 1.0;
    if (viewTimeRange > 60.0) timeStep = 10.0;
    if (viewTimeRange > 300.0) timeStep = 60.0;
    if (viewTimeRange > 1800.0) timeStep = 300.0;
    if (viewTimeRange > 7200.0) timeStep = 1800.0;
    if (viewTimeRange < 5.0) timeStep = 0.5;
    if (viewTimeRange < 2.0) timeStep = 0.25;
    
    double gridStartTime = std::ceil(viewStartTime / timeStep) * timeStep;
    
    for (double t = gridStartTime; t < viewStartTime + viewTimeRange; t += timeStep)
    {
        int x = snapToPixel(timeToX(t));
        
        g.setColour(gridColour);
        g.drawVerticalLine(x, 0.0f, static_cast<float>(height));
        
        g.setColour(textColour.withAlpha(0.7f));
        
        juce::String timeLabel;
        if (t >= 3600.0)
        {
            int hours = static_cast<int>(t) / 3600;
            int minutes = (static_cast<int>(t) % 3600) / 60;
            int seconds = static_cast<int>(t) % 60;
            timeLabel = juce::String::formatted("%d:%02d:%02d", hours, minutes, seconds);
        }
        else if (t >= 60.0)
        {
            int minutes = static_cast<int>(t) / 60;
            int seconds = static_cast<int>(t) % 60;
            timeLabel = juce::String::formatted("%d:%02d", minutes, seconds);
        }
        else
        {
            timeLabel = juce::String(t, 1) + "s";
        }
        
        g.drawText(timeLabel, x - 30, height - 15, 60, 12,
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
    
    if (!momentaryPath.isEmpty())
    {
        g.setColour(momentaryColour);
        g.strokePath(momentaryPath, juce::PathStrokeType(1.5f, 
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    // Draw Short-term SECOND (on top)
    if (!shortTermFillPath.isEmpty())
    {
        g.setColour(shortTermColour.withAlpha(0.35f));
        g.fillPath(shortTermFillPath);
    }
    
    if (!shortTermPath.isEmpty())
    {
        g.setColour(shortTermColour);
        g.strokePath(shortTermPath, juce::PathStrokeType(2.0f,
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
    
    juce::String momentaryStr = currentMomentary > -100.0f ? 
        juce::String(currentMomentary, 1) + " LUFS" : "-inf LUFS";
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
    
    juce::String shortTermStr = currentShortTerm > -100.0f ? 
        juce::String(currentShortTerm, 1) + " LUFS" : "-inf LUFS";
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
        timeStr = juce::String(viewTimeRange / 3600.0, 2) + " hours";
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
    
    // LOD level info
    juce::String lodStr = "LOD: " + juce::String(cachedRenderData.lodLevel);
    
    // Points info
    size_t numPoints = cachedRenderData.useMinMax ? 
        cachedRenderData.minMaxPoints.size() : cachedRenderData.points.size();
    juce::String pointsStr = juce::String(numPoints) + " pts";
    
    juce::String infoStr = "X: " + timeStr + " | Y: " + lufsStr + " | " + lodStr + " | " + pointsStr;
    
    g.setFont(10.0f);
    g.setColour(textColour.withAlpha(0.6f));
    
    int textWidth = 300;
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

int LoudnessHistoryDisplay::snapToPixel(float value) const
{
    return static_cast<int>(std::round(value));
}