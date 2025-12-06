#include "LoudnessHistoryDisplay.h"
#include <cmath>

LoudnessHistoryDisplay::LoudnessHistoryDisplay(LoudnessDataStore& store)
    : dataStore(store)
{
    setOpaque(true);
    
    // Enable high-quality rendering
    setBufferedToImage(true);
    
    // Start update timer at 60 FPS for smooth scrolling
    startTimerHz(60);
}

LoudnessHistoryDisplay::~LoudnessHistoryDisplay()
{
    stopTimer();
}

void LoudnessHistoryDisplay::timerCallback()
{
    // Auto-scroll: keep view at current time
    double currentTime = dataStore.getCurrentTime();
    double range = viewTimeRange.load(std::memory_order_relaxed);
    double targetStart = currentTime - range * 0.9;
    
    // Smooth scrolling animation
    double currentStart = viewStartTime.load(std::memory_order_relaxed);
    double newStart = currentStart + (targetStart - currentStart) * kScrollSmoothing;
    
    // Only update if there's significant change (prevents micro-jitter)
    if (std::abs(newStart - currentStart) > 0.0001)
    {
        viewStartTime.store(newStart, std::memory_order_relaxed);
        needsDataUpdate = true;
    }
    
    repaint();
}

void LoudnessHistoryDisplay::setCurrentLoudness(float momentary, float shortTerm)
{
    currentMomentary.store(momentary, std::memory_order_relaxed);
    currentShortTerm.store(shortTerm, std::memory_order_relaxed);
}

void LoudnessHistoryDisplay::paint(juce::Graphics& g)
{
    updateCachedData();
    
    drawBackground(g);
    drawGrid(g);
    drawCurves(g);
    drawCurrentValues(g);
}

void LoudnessHistoryDisplay::resized()
{
    needsDataUpdate = true;
}

void LoudnessHistoryDisplay::mouseWheelMove(const juce::MouseEvent& event,
                                             const juce::MouseWheelDetails& wheel)
{
    const float zoomFactor = 1.1f;
    
    bool shiftHeld = event.mods.isShiftDown();
    
    if (shiftHeld)
    {
        // Y-axis zoom (LUFS range)
        float currentMin = viewMinLufs.load(std::memory_order_relaxed);
        float currentMax = viewMaxLufs.load(std::memory_order_relaxed);
        float range = currentMax - currentMin;
        
        // Zoom centered on mouse Y position
        float mouseY = event.position.y;
        float mouseRatio = mouseY / static_cast<float>(getHeight());
        float mouseLufs = currentMax - mouseRatio * range;
        
        float newRange = range;
        if (wheel.deltaY > 0)
            newRange = range / zoomFactor;
        else if (wheel.deltaY < 0)
            newRange = range * zoomFactor;
        
        newRange = juce::jlimit(kMinLufsRange, kMaxLufsRange, newRange);
        
        // Keep mouse position anchored
        float newMax = mouseLufs + mouseRatio * newRange;
        float newMin = newMax - newRange;
        
        // Clamp to valid range
        if (newMax > 0.0f)
        {
            newMax = 0.0f;
            newMin = newMax - newRange;
        }
        if (newMin < -100.0f)
        {
            newMin = -100.0f;
            newMax = newMin + newRange;
        }
        
        viewMinLufs.store(newMin, std::memory_order_relaxed);
        viewMaxLufs.store(newMax, std::memory_order_relaxed);
    }
    else
    {
        // X-axis zoom (time range)
        double currentRange = viewTimeRange.load(std::memory_order_relaxed);
        double currentStart = viewStartTime.load(std::memory_order_relaxed);
        
        // Zoom centered on mouse X position
        float mouseX = event.position.x;
        double mouseRatio = static_cast<double>(mouseX) / static_cast<double>(getWidth());
        double mouseTime = currentStart + mouseRatio * currentRange;
        
        double newRange = currentRange;
        if (wheel.deltaY > 0)
            newRange = currentRange / zoomFactor;
        else if (wheel.deltaY < 0)
            newRange = currentRange * zoomFactor;
        
        newRange = juce::jlimit(kMinTimeRange, kMaxTimeRange, newRange);
        
        // Keep mouse position anchored
        double newStart = mouseTime - mouseRatio * newRange;
        
        viewTimeRange.store(newRange, std::memory_order_relaxed);
        viewStartTime.store(newStart, std::memory_order_relaxed);
    }
    
    needsDataUpdate = true;
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
    
    // Pan the view
    double timeRange = viewTimeRange.load(std::memory_order_relaxed);
    double timeDelta = static_cast<double>(-dx) * timeRange / static_cast<double>(getWidth());
    
    double currentStart = viewStartTime.load(std::memory_order_relaxed);
    viewStartTime.store(currentStart + timeDelta, std::memory_order_relaxed);
    
    float lufsRange = viewMaxLufs.load(std::memory_order_relaxed) - 
                      viewMinLufs.load(std::memory_order_relaxed);
    float lufsDelta = dy * lufsRange / static_cast<float>(getHeight());
    
    viewMinLufs.store(viewMinLufs.load(std::memory_order_relaxed) + lufsDelta, 
                      std::memory_order_relaxed);
    viewMaxLufs.store(viewMaxLufs.load(std::memory_order_relaxed) + lufsDelta, 
                      std::memory_order_relaxed);
    
    needsDataUpdate = true;
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
    auto bounds = getLocalBounds().toFloat();
    
    g.setColour(gridColour);
    
    float minLufs = viewMinLufs.load(std::memory_order_relaxed);
    float maxLufs = viewMaxLufs.load(std::memory_order_relaxed);
    float lufsRange = maxLufs - minLufs;
    
    // Horizontal grid lines (LUFS levels)
    float gridStep = 6.0f; // 6 LUFS steps
    if (lufsRange > 40.0f) gridStep = 12.0f;
    if (lufsRange < 20.0f) gridStep = 3.0f;
    
    float startLufs = std::ceil(minLufs / gridStep) * gridStep;
    
    g.setFont(10.0f);
    for (float lufs = startLufs; lufs <= maxLufs; lufs += gridStep)
    {
        float y = loudnessToY(lufs);
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, bounds.getWidth());
        
        // Draw label
        g.setColour(textColour.withAlpha(0.7f));
        g.drawText(juce::String(static_cast<int>(lufs)) + " LUFS",
                   5, static_cast<int>(y) - 12, 50, 12,
                   juce::Justification::left);
        g.setColour(gridColour);
    }
    
    // Vertical grid lines (time)
    double startTime = viewStartTime.load(std::memory_order_relaxed);
    double timeRange = viewTimeRange.load(std::memory_order_relaxed);
    
    double timeStep = 1.0;
    if (timeRange > 60.0) timeStep = 10.0;
    if (timeRange > 300.0) timeStep = 60.0;
    if (timeRange > 1800.0) timeStep = 300.0;
    if (timeRange > 7200.0) timeStep = 1800.0;
    if (timeRange < 5.0) timeStep = 0.5;
    
    double gridStartTime = std::ceil(startTime / timeStep) * timeStep;
    
    for (double t = gridStartTime; t < startTime + timeRange; t += timeStep)
    {
        float x = timeToX(t);
        g.drawVerticalLine(static_cast<int>(x), 0.0f, bounds.getHeight());
        
        // Draw time label
        g.setColour(textColour.withAlpha(0.7f));
        
        juce::String timeLabel;
        if (timeStep >= 60.0)
        {
            int minutes = static_cast<int>(t) / 60;
            int seconds = static_cast<int>(t) % 60;
            timeLabel = juce::String::formatted("%d:%02d", minutes, seconds);
        }
        else
        {
            timeLabel = juce::String(t, 1) + "s";
        }
        
        g.drawText(timeLabel, static_cast<int>(x) - 25, 
                   static_cast<int>(bounds.getHeight()) - 15, 50, 12,
                   juce::Justification::centred);
        g.setColour(gridColour);
    }
}

void LoudnessHistoryDisplay::drawCurves(juce::Graphics& g)
{
    if (cachedRenderData.points.empty() && cachedRenderData.minMaxBands.empty())
        return;
    
    auto bounds = getLocalBounds().toFloat();
    
    double startTime = viewStartTime.load(std::memory_order_relaxed);
    double timeRange = viewTimeRange.load(std::memory_order_relaxed);
    
    if (cachedRenderData.useMinMax && !cachedRenderData.minMaxBands.empty())
    {
        // Draw envelope bands for zoomed out view
        auto& minMax = cachedRenderData.minMaxBands;
        
        // Calculate time step
        double timeStep = timeRange / static_cast<double>(minMax.size());
        
        // Momentary envelope
        juce::Path momentaryEnvelope;
        juce::Path shortTermEnvelope;
        
        bool firstPoint = true;
        for (size_t i = 0; i < minMax.size(); ++i)
        {
            double t = startTime + static_cast<double>(i) * timeStep;
            float x = timeToX(t);
            float yMax = loudnessToY(minMax[i].maxMomentary);
            
            if (firstPoint)
            {
                momentaryEnvelope.startNewSubPath(x, yMax);
                firstPoint = false;
            }
            else
            {
                momentaryEnvelope.lineTo(x, yMax);
            }
        }
        
        // Draw back along min values
        for (int i = static_cast<int>(minMax.size()) - 1; i >= 0; --i)
        {
            double t = startTime + static_cast<double>(i) * timeStep;
            float x = timeToX(t);
            float yMin = loudnessToY(minMax[static_cast<size_t>(i)].minMomentary);
            momentaryEnvelope.lineTo(x, yMin);
        }
        momentaryEnvelope.closeSubPath();
        
        // Fill envelope with semi-transparent color
        g.setColour(momentaryColour.withAlpha(0.3f));
        g.fillPath(momentaryEnvelope);
        
        // Draw short-term envelope
        firstPoint = true;
        for (size_t i = 0; i < minMax.size(); ++i)
        {
            double t = startTime + static_cast<double>(i) * timeStep;
            float x = timeToX(t);
            float yMax = loudnessToY(minMax[i].maxShortTerm);
            
            if (firstPoint)
            {
                shortTermEnvelope.startNewSubPath(x, yMax);
                firstPoint = false;
            }
            else
            {
                shortTermEnvelope.lineTo(x, yMax);
            }
        }
        
        for (int i = static_cast<int>(minMax.size()) - 1; i >= 0; --i)
        {
            double t = startTime + static_cast<double>(i) * timeStep;
            float x = timeToX(t);
            float yMin = loudnessToY(minMax[static_cast<size_t>(i)].minShortTerm);
            shortTermEnvelope.lineTo(x, yMin);
        }
        shortTermEnvelope.closeSubPath();
        
        g.setColour(shortTermColour.withAlpha(0.3f));
        g.fillPath(shortTermEnvelope);
    }
    
    // Draw line curves
    if (!cachedRenderData.points.empty())
    {
        juce::Path momentaryLine;
        juce::Path shortTermLine;
        
        bool firstPoint = true;
        for (const auto& point : cachedRenderData.points)
        {
            float x = timeToX(point.timestamp);
            float yM = loudnessToY(point.momentary);
            float yS = loudnessToY(point.shortTerm);
            
            // Clamp to visible area
            if (x < -10 || x > bounds.getWidth() + 10)
                continue;
            
            if (firstPoint)
            {
                momentaryLine.startNewSubPath(x, yM);
                shortTermLine.startNewSubPath(x, yS);
                firstPoint = false;
            }
            else
            {
                momentaryLine.lineTo(x, yM);
                shortTermLine.lineTo(x, yS);
            }
        }
        
        // Draw short-term first (behind)
        g.setColour(shortTermColour);
        g.strokePath(shortTermLine, juce::PathStrokeType(1.5f, 
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        
        // Draw momentary on top
        g.setColour(momentaryColour);
        g.strokePath(momentaryLine, juce::PathStrokeType(2.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void LoudnessHistoryDisplay::drawCurrentValues(juce::Graphics& g)
{
    float momentary = currentMomentary.load(std::memory_order_relaxed);
    float shortTerm = currentShortTerm.load(std::memory_order_relaxed);
    
    // Draw current value boxes
    auto bounds = getLocalBounds();
    int boxWidth = 120;
    int boxHeight = 40;
    int margin = 10;
    
    // Momentary box
    juce::Rectangle<int> momentaryBox(margin, margin, boxWidth, boxHeight);
    g.setColour(momentaryColour.withAlpha(0.8f));
    g.fillRoundedRectangle(momentaryBox.toFloat(), 5.0f);
    g.setColour(juce::Colours::white);
    g.setFont(10.0f);
    g.drawText("Momentary", momentaryBox.removeFromTop(14).reduced(5, 0), 
               juce::Justification::left);
    g.setFont(18.0f);
    
    juce::String momentaryStr = momentary > -100.0f ? 
        juce::String(momentary, 1) + " LUFS" : "-inf LUFS";
    g.drawText(momentaryStr, momentaryBox.reduced(5, 0), 
               juce::Justification::left);
    
    // Short-term box
    juce::Rectangle<int> shortTermBox(margin + boxWidth + margin, margin, boxWidth, boxHeight);
    g.setColour(shortTermColour.withAlpha(0.8f));
    g.fillRoundedRectangle(shortTermBox.toFloat(), 5.0f);
    g.setColour(juce::Colours::white);
    g.setFont(10.0f);
    g.drawText("Short-term", shortTermBox.removeFromTop(14).reduced(5, 0), 
               juce::Justification::left);
    g.setFont(18.0f);
    
    juce::String shortTermStr = shortTerm > -100.0f ? 
        juce::String(shortTerm, 1) + " LUFS" : "-inf LUFS";
    g.drawText(shortTermStr, shortTermBox.reduced(5, 0), 
               juce::Justification::left);
    
    // Draw legend
    int legendY = bounds.getHeight() - 25;
    g.setFont(11.0f);
    
    g.setColour(momentaryColour);
    g.fillRect(margin, legendY, 15, 3);
    g.setColour(textColour);
    g.drawText("Momentary (400ms)", margin + 20, legendY - 6, 120, 15,
               juce::Justification::left);
    
    g.setColour(shortTermColour);
    g.fillRect(margin + 140, legendY, 15, 3);
    g.setColour(textColour);
    g.drawText("Short-term (3s)", margin + 160, legendY - 6, 100, 15,
               juce::Justification::left);
}

float LoudnessHistoryDisplay::timeToX(double time) const
{
    double startTime = viewStartTime.load(std::memory_order_relaxed);
    double timeRange = viewTimeRange.load(std::memory_order_relaxed);
    return static_cast<float>((time - startTime) / timeRange * static_cast<double>(getWidth()));
}

float LoudnessHistoryDisplay::loudnessToY(float lufs) const
{
    float minLufs = viewMinLufs.load(std::memory_order_relaxed);
    float maxLufs = viewMaxLufs.load(std::memory_order_relaxed);
    float range = maxLufs - minLufs;
    return (maxLufs - lufs) / range * static_cast<float>(getHeight());
}

double LoudnessHistoryDisplay::xToTime(float x) const
{
    double startTime = viewStartTime.load(std::memory_order_relaxed);
    double timeRange = viewTimeRange.load(std::memory_order_relaxed);
    return startTime + (static_cast<double>(x) / static_cast<double>(getWidth())) * timeRange;
}

float LoudnessHistoryDisplay::yToLoudness(float y) const
{
    float minLufs = viewMinLufs.load(std::memory_order_relaxed);
    float maxLufs = viewMaxLufs.load(std::memory_order_relaxed);
    float range = maxLufs - minLufs;
    return maxLufs - (y / static_cast<float>(getHeight())) * range;
}

void LoudnessHistoryDisplay::updateCachedData()
{
    double startTime = viewStartTime.load(std::memory_order_relaxed);
    double timeRange = viewTimeRange.load(std::memory_order_relaxed);
    double endTime = startTime + timeRange;
    int width = getWidth();
    
    // Only update if view changed significantly
    if (needsDataUpdate || 
        std::abs(startTime - cachedStartTime) > timeRange * 0.01 ||
        std::abs(endTime - cachedEndTime) > timeRange * 0.01 ||
        width != cachedWidth)
    {
        cachedRenderData = dataStore.getDataForTimeRange(startTime, endTime, width);
        cachedStartTime = startTime;
        cachedEndTime = endTime;
        cachedWidth = width;
        needsDataUpdate = false;
    }
}