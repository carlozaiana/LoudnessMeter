#include "LoudnessHistoryDisplay.h"
#include <cmath>

LoudnessHistoryDisplay::LoudnessHistoryDisplay(LoudnessDataStore& store)
    : dataStore(store)
{
    setOpaque(true);
    startTimerHz(30);
}

LoudnessHistoryDisplay::~LoudnessHistoryDisplay()
{
    stopTimer();
}

void LoudnessHistoryDisplay::timerCallback()
{
    double currentTime = dataStore.getCurrentTime();
    double targetStart = currentTime - viewTimeRange * 0.9;
    
    double newStart = viewStartTime + (targetStart - viewStartTime) * kScrollSmoothing;
    
    if (std::abs(newStart - viewStartTime) > 0.001)
    {
        viewStartTime = newStart;
    }
    
    repaint();
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
    
    double endTime = viewStartTime + viewTimeRange;
    int width = getWidth();
    
    if (width > 0)
    {
        cachedRenderData = dataStore.getDataForTimeRange(viewStartTime, endTime, width);
        cachedStartTime = viewStartTime;
        cachedEndTime = endTime;
        cachedWidth = width;
    }
    
    drawBackground(g);
    drawGrid(g);
    drawCurves(g);
    drawCurrentValues(g);
}

void LoudnessHistoryDisplay::resized()
{
}

void LoudnessHistoryDisplay::mouseWheelMove(const juce::MouseEvent& event,
                                             const juce::MouseWheelDetails& wheel)
{
    const float zoomFactor = 1.1f;
    
    if (event.mods.isShiftDown())
    {
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
            viewMinLufs = viewMaxLufs - newRange;
        }
        if (viewMinLufs < -100.0f)
        {
            viewMinLufs = -100.0f;
            viewMaxLufs = viewMinLufs + newRange;
        }
    }
    else
    {
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
    
    float lufsRange = viewMaxLufs - viewMinLufs;
    float gridStep = 6.0f;
    if (lufsRange > 40.0f) gridStep = 12.0f;
    if (lufsRange < 20.0f) gridStep = 3.0f;
    
    float startLufs = std::ceil(viewMinLufs / gridStep) * gridStep;
    
    g.setFont(10.0f);
    for (float lufs = startLufs; lufs <= viewMaxLufs; lufs += gridStep)
    {
        float y = loudnessToY(lufs);
        g.setColour(gridColour);
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, bounds.getWidth());
        
        g.setColour(textColour.withAlpha(0.7f));
        g.drawText(juce::String(static_cast<int>(lufs)) + " LUFS",
                   5, static_cast<int>(y) - 12, 60, 12,
                   juce::Justification::left);
    }
    
    double timeStep = 1.0;
    if (viewTimeRange > 60.0) timeStep = 10.0;
    if (viewTimeRange > 300.0) timeStep = 60.0;
    if (viewTimeRange > 1800.0) timeStep = 300.0;
    if (viewTimeRange > 7200.0) timeStep = 1800.0;
    if (viewTimeRange < 5.0) timeStep = 0.5;
    
    double gridStartTime = std::ceil(viewStartTime / timeStep) * timeStep;
    
    for (double t = gridStartTime; t < viewStartTime + viewTimeRange; t += timeStep)
    {
        float x = timeToX(t);
        g.setColour(gridColour);
        g.drawVerticalLine(static_cast<int>(x), 0.0f, bounds.getHeight());
        
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
    }
}

void LoudnessHistoryDisplay::drawCurves(juce::Graphics& g)
{
    if (cachedRenderData.points.empty())
        return;
    
    auto bounds = getLocalBounds().toFloat();
    
    juce::Path momentaryLine;
    juce::Path shortTermLine;
    
    bool firstPoint = true;
    
    for (const auto& point : cachedRenderData.points)
    {
        float x = timeToX(point.timestamp);
        
        if (x < -50 || x > bounds.getWidth() + 50)
            continue;
        
        float yM = loudnessToY(point.momentary);
        float yS = loudnessToY(point.shortTerm);
        
        // Clamp Y values
        yM = juce::jlimit(0.0f, bounds.getHeight(), yM);
        yS = juce::jlimit(0.0f, bounds.getHeight(), yS);
        
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
    
    if (!shortTermLine.isEmpty())
    {
        g.setColour(shortTermColour);
        g.strokePath(shortTermLine, juce::PathStrokeType(1.5f, 
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    if (!momentaryLine.isEmpty())
    {
        g.setColour(momentaryColour);
        g.strokePath(momentaryLine, juce::PathStrokeType(2.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void LoudnessHistoryDisplay::drawCurrentValues(juce::Graphics& g)
{
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
    
    juce::String momentaryStr = currentMomentary > -100.0f ? 
        juce::String(currentMomentary, 1) + " LUFS" : "-inf LUFS";
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
    
    juce::String shortTermStr = currentShortTerm > -100.0f ? 
        juce::String(currentShortTerm, 1) + " LUFS" : "-inf LUFS";
    g.drawText(shortTermStr, shortTermBox.reduced(5, 0), 
               juce::Justification::left);
    
    // Legend
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
    return static_cast<float>((time - viewStartTime) / viewTimeRange * static_cast<double>(getWidth()));
}

float LoudnessHistoryDisplay::loudnessToY(float lufs) const
{
    float range = viewMaxLufs - viewMinLufs;
    if (range <= 0.0f) return 0.0f;
    return (viewMaxLufs - lufs) / range * static_cast<float>(getHeight());
}