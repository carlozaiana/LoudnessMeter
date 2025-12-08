#include "LoudnessHistoryDisplay.h"
#include <cmath>

LoudnessHistoryDisplay::LoudnessHistoryDisplay(LoudnessDataStore& store)
    : dataStore(store)
{
    setOpaque(true);
    initializeImageStrips();
    startTimerHz(20); // 20 Hz update rate = 50ms
}

LoudnessHistoryDisplay::~LoudnessHistoryDisplay()
{
    stopTimer();
}

void LoudnessHistoryDisplay::initializeImageStrips()
{
    for (int lod = 0; lod < kNumLodLevels; ++lod)
    {
        const auto& config = kLodConfigs[static_cast<size_t>(lod)];
        imageStrips[static_cast<size_t>(lod)].image = juce::Image(
            juce::Image::ARGB, config.stripWidth, kImageHeight, true);
        imageStrips[static_cast<size_t>(lod)].currentColumn = 0;
        imageStrips[static_cast<size_t>(lod)].lastRenderedTime = 0.0;
        imageStrips[static_cast<size_t>(lod)].needsFullRedraw = true;
    }
}

void LoudnessHistoryDisplay::timerCallback()
{
    double currentTime = dataStore.getCurrentTime();
    
    if (currentTime > lastUpdateTime)
    {
        updateImageStrips();
        lastUpdateTime = currentTime;
        repaint();
    }
}

void LoudnessHistoryDisplay::updateImageStrips()
{
    double currentTime = dataStore.getCurrentTime();
    
    // Always update LOD 0 first
    auto& lod0 = imageStrips[0];
    const auto& config0 = kLodConfigs[0];
    
    if (lod0.needsFullRedraw)
    {
        // Clear and redraw entire strip
        lod0.image.clear(lod0.image.getBounds(), backgroundColour);
        lod0.currentColumn = 0;
        lod0.lastRenderedTime = 0.0;
        lod0.needsFullRedraw = false;
    }
    
    // Calculate how many new columns to render
    double timeSinceLastRender = currentTime - lod0.lastRenderedTime;
    int columnsToRender = static_cast<int>(timeSinceLastRender / config0.secondsPerPixel);
    
    if (columnsToRender > 0)
    {
        // Limit to strip width to prevent excessive processing
        columnsToRender = std::min(columnsToRender, config0.stripWidth);
        
        for (int i = 0; i < columnsToRender; ++i)
        {
            double columnStartTime = lod0.lastRenderedTime + i * config0.secondsPerPixel;
            double columnEndTime = columnStartTime + config0.secondsPerPixel;
            
            // Wrap column index
            int colIdx = lod0.currentColumn % config0.stripWidth;
            
            renderColumnToStrip(0, colIdx, columnStartTime, columnEndTime);
            lod0.currentColumn++;
        }
        
        lod0.lastRenderedTime = lod0.lastRenderedTime + columnsToRender * config0.secondsPerPixel;
        
        // Trigger downsampling to higher LOD levels
        downsampleToHigherLODs(lod0.currentColumn);
    }
}

void LoudnessHistoryDisplay::renderColumnToStrip(int lodLevel, int columnIndex, 
                                                   double startTime, double endTime)
{
    auto& strip = imageStrips[static_cast<size_t>(lodLevel)];
    
    // Get data points in this time range
    auto points = dataStore.getPointsInRange(startTime, endTime);
    
    // Find min/max for this column
    float minMomentary = 100.0f, maxMomentary = -100.0f;
    float minShortTerm = 100.0f, maxShortTerm = -100.0f;
    
    for (const auto& p : points)
    {
        if (p.momentary > -100.0f)
        {
            minMomentary = std::min(minMomentary, p.momentary);
            maxMomentary = std::max(maxMomentary, p.momentary);
        }
        if (p.shortTerm > -100.0f)
        {
            minShortTerm = std::min(minShortTerm, p.shortTerm);
            maxShortTerm = std::max(maxShortTerm, p.shortTerm);
        }
    }
    
    // Clear this column to background
    juce::Graphics g(strip.image);
    g.setColour(backgroundColour);
    g.fillRect(columnIndex, 0, 1, kImageHeight);
    
    // Draw momentary (behind)
    if (maxMomentary > -100.0f)
    {
        int yTop = static_cast<int>(lufsToImageY(maxMomentary));
        int yBottom = static_cast<int>(lufsToImageY(minMomentary));
        
        // Clamp to image bounds
        yTop = juce::jlimit(0, kImageHeight - 1, yTop);
        yBottom = juce::jlimit(0, kImageHeight - 1, yBottom);
        
        if (yBottom >= yTop)
        {
            // Fill envelope
            g.setColour(momentaryColour.withAlpha(0.4f));
            g.fillRect(columnIndex, yTop, 1, yBottom - yTop + 1);
            
            // Draw peak line
            g.setColour(momentaryColour);
            g.fillRect(columnIndex, yTop, 1, 1);
        }
    }
    
    // Draw short-term (on top)
    if (maxShortTerm > -100.0f)
    {
        int yTop = static_cast<int>(lufsToImageY(maxShortTerm));
        int yBottom = static_cast<int>(lufsToImageY(minShortTerm));
        
        yTop = juce::jlimit(0, kImageHeight - 1, yTop);
        yBottom = juce::jlimit(0, kImageHeight - 1, yBottom);
        
        if (yBottom >= yTop)
        {
            // Fill envelope
            g.setColour(shortTermColour.withAlpha(0.5f));
            g.fillRect(columnIndex, yTop, 1, yBottom - yTop + 1);
            
            // Draw peak line
            g.setColour(shortTermColour);
            g.fillRect(columnIndex, yTop, 1, 1);
        }
    }
}

void LoudnessHistoryDisplay::downsampleToHigherLODs(int fromColumn)
{
    // For each higher LOD level, check if we have enough new columns to create one new column
    for (int lod = 1; lod < kNumLodLevels; ++lod)
    {
        const auto& prevConfig = kLodConfigs[static_cast<size_t>(lod - 1)];
        const auto& thisConfig = kLodConfigs[static_cast<size_t>(lod)];
        auto& thisStrip = imageStrips[static_cast<size_t>(lod)];
        auto& prevStrip = imageStrips[static_cast<size_t>(lod - 1)];
        
        // Calculate ratio between LOD levels
        int columnsPerPixel = static_cast<int>(thisConfig.secondsPerPixel / prevConfig.secondsPerPixel);
        
        // Check if we should render a new column at this LOD
        double currentTime = dataStore.getCurrentTime();
        double timeSinceLastRender = currentTime - thisStrip.lastRenderedTime;
        
        if (timeSinceLastRender >= thisConfig.secondsPerPixel || thisStrip.needsFullRedraw)
        {
            if (thisStrip.needsFullRedraw)
            {
                thisStrip.image.clear(thisStrip.image.getBounds(), backgroundColour);
                thisStrip.currentColumn = 0;
                thisStrip.lastRenderedTime = 0.0;
                thisStrip.needsFullRedraw = false;
            }
            
            int columnsToRender = static_cast<int>(timeSinceLastRender / thisConfig.secondsPerPixel);
            columnsToRender = std::min(columnsToRender, thisConfig.stripWidth);
            
            for (int i = 0; i < columnsToRender; ++i)
            {
                double columnStartTime = thisStrip.lastRenderedTime + i * thisConfig.secondsPerPixel;
                double columnEndTime = columnStartTime + thisConfig.secondsPerPixel;
                
                int colIdx = thisStrip.currentColumn % thisConfig.stripWidth;
                renderColumnToStrip(lod, colIdx, columnStartTime, columnEndTime);
                thisStrip.currentColumn++;
            }
            
            thisStrip.lastRenderedTime = thisStrip.lastRenderedTime + columnsToRender * thisConfig.secondsPerPixel;
        }
    }
}

float LoudnessHistoryDisplay::lufsToImageY(float lufs) const
{
    // Map LUFS to image Y coordinate
    // kMaxLufs (0) -> 0 (top)
    // kMinLufs (-90) -> kImageHeight (bottom)
    float normalized = (kMaxLufs - lufs) / kLufsRange;
    return normalized * static_cast<float>(kImageHeight);
}

int LoudnessHistoryDisplay::getLodLevelForTimeRange(double timeRange) const
{
    for (int lod = 0; lod < kNumLodLevels; ++lod)
    {
        if (timeRange <= kLodConfigs[static_cast<size_t>(lod)].maxTimeRange)
            return lod;
    }
    return kNumLodLevels - 1;
}

void LoudnessHistoryDisplay::setCurrentLoudness(float momentary, float shortTerm)
{
    currentMomentary = momentary;
    currentShortTerm = shortTerm;
}

void LoudnessHistoryDisplay::paint(juce::Graphics& g)
{
    drawBackground(g);
    drawFromImageStrips(g);
    drawGrid(g);
    drawCurrentValues(g);
    drawZoomInfo(g);
}

void LoudnessHistoryDisplay::drawBackground(juce::Graphics& g)
{
    g.fillAll(backgroundColour);
}

void LoudnessHistoryDisplay::drawFromImageStrips(juce::Graphics& g)
{
    int width = getWidth();
    int height = getHeight();
    
    if (width <= 0 || height <= 0)
        return;
    
    double currentTime = dataStore.getCurrentTime();
    
    // Select appropriate LOD level
    currentLodLevel = getLodLevelForTimeRange(viewTimeRange);
    const auto& config = kLodConfigs[static_cast<size_t>(currentLodLevel)];
    const auto& strip = imageStrips[static_cast<size_t>(currentLodLevel)];
    
    // Calculate which portion of the image strip to draw
    // We want to show data ending at currentTime, starting at (currentTime - viewTimeRange)
    double startTime = currentTime - viewTimeRange;
    
    // Calculate pixel positions in the strip
    int endColumn = strip.currentColumn;
    int pixelsToShow = static_cast<int>(viewTimeRange / config.secondsPerPixel);
    pixelsToShow = std::min(pixelsToShow, config.stripWidth);
    int startColumn = endColumn - pixelsToShow;
    
    // Calculate source and destination rectangles
    // The strip is a ring buffer, so we may need to draw in two parts
    
    // Calculate Y scaling based on current view range
    float viewRange = viewMaxLufs - viewMinLufs;
    float yScale = static_cast<float>(height) / (viewRange / kLufsRange * static_cast<float>(kImageHeight));
    
    // Calculate Y offset in the source image
    float sourceYStart = (kMaxLufs - viewMaxLufs) / kLufsRange * static_cast<float>(kImageHeight);
    float sourceYEnd = (kMaxLufs - viewMinLufs) / kLufsRange * static_cast<float>(kImageHeight);
    float sourceHeight = sourceYEnd - sourceYStart;
    
    // Scale factor for X axis
    float xScale = static_cast<float>(width) / static_cast<float>(pixelsToShow);
    
    // Handle ring buffer wraparound
    if (startColumn < 0)
    {
        // Need to draw from two parts of the buffer
        int part1Start = (startColumn + config.stripWidth) % config.stripWidth;
        int part1Width = config.stripWidth - part1Start;
        int part2Width = endColumn;
        
        // Destination widths
        float totalPixels = static_cast<float>(part1Width + part2Width);
        float part1DestWidth = static_cast<float>(part1Width) / totalPixels * static_cast<float>(width);
        float part2DestWidth = static_cast<float>(width) - part1DestWidth;
        
        // Draw part 1 (older data from end of buffer)
        juce::Rectangle<float> srcRect1(
            static_cast<float>(part1Start), sourceYStart,
            static_cast<float>(part1Width), sourceHeight
        );
        juce::Rectangle<float> destRect1(
            0.0f, 0.0f,
            part1DestWidth, static_cast<float>(height)
        );
        g.drawImage(strip.image, destRect1, juce::RectanglePlacement::stretchToFit, false,
                    srcRect1);
        
        // Draw part 2 (newer data from start of buffer)
        if (part2Width > 0)
        {
            juce::Rectangle<float> srcRect2(
                0.0f, sourceYStart,
                static_cast<float>(part2Width), sourceHeight
            );
            juce::Rectangle<float> destRect2(
                part1DestWidth, 0.0f,
                part2DestWidth, static_cast<float>(height)
            );
            g.drawImage(strip.image, destRect2, juce::RectanglePlacement::stretchToFit, false,
                        srcRect2);
        }
    }
    else
    {
        // Simple case: contiguous region
        startColumn = startColumn % config.stripWidth;
        
        juce::Rectangle<float> srcRect(
            static_cast<float>(startColumn), sourceYStart,
            static_cast<float>(pixelsToShow), sourceHeight
        );
        juce::Rectangle<float> destRect(
            0.0f, 0.0f,
            static_cast<float>(width), static_cast<float>(height)
        );
        
        g.drawImage(strip.image, destRect, juce::RectanglePlacement::stretchToFit, false,
                    srcRect);
    }
}

void LoudnessHistoryDisplay::drawGrid(juce::Graphics& g)
{
    int width = getWidth();
    int height = getHeight();
    
    double currentTime = dataStore.getCurrentTime();
    
    // Horizontal grid lines (LUFS levels)
    float lufsRange = viewMaxLufs - viewMinLufs;
    float gridStep = 6.0f;
    if (lufsRange > 40.0f) gridStep = 12.0f;
    if (lufsRange < 20.0f) gridStep = 3.0f;
    
    float startLufs = std::ceil(viewMinLufs / gridStep) * gridStep;
    
    g.setFont(10.0f);
    for (float lufs = startLufs; lufs <= viewMaxLufs; lufs += gridStep)
    {
        float normalizedY = (viewMaxLufs - lufs) / lufsRange;
        int y = static_cast<int>(normalizedY * static_cast<float>(height));
        
        g.setColour(gridColour);
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(width));
        
        g.setColour(textColour.withAlpha(0.7f));
        g.drawText(juce::String(static_cast<int>(lufs)) + " LUFS",
                   5, y - 12, 60, 12,
                   juce::Justification::left);
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
    
    double startTime = currentTime - viewTimeRange;
    double gridStartTime = std::floor(startTime / timeStep) * timeStep;
    
    for (double t = gridStartTime; t <= currentTime; t += timeStep)
    {
        if (t < startTime) continue;
        
        // Calculate X position (right edge is current time)
        float normalizedX = static_cast<float>((t - startTime) / viewTimeRange);
        int x = static_cast<int>(normalizedX * static_cast<float>(width));
        
        g.setColour(gridColour);
        g.drawVerticalLine(x, 0.0f, static_cast<float>(height));
        
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
        
        g.drawText(timeLabel, x - 30, height - 15, 60, 12,
                   juce::Justification::centred);
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
        timeStr = juce::String(viewTimeRange / 3600.0, 2) + " hrs";
    else if (viewTimeRange >= 60.0)
        timeStr = juce::String(viewTimeRange / 60.0, 1) + " min";
    else
        timeStr = juce::String(viewTimeRange, 1) + " sec";
    
    // Format LUFS range
    float lufsRange = viewMaxLufs - viewMinLufs;
    juce::String lufsStr = juce::String(static_cast<int>(lufsRange)) + " dB";
    
    // LOD info
    juce::String lodStr = "LOD " + juce::String(currentLodLevel);
    const auto& config = kLodConfigs[static_cast<size_t>(currentLodLevel)];
    
    juce::String resStr;
    if (config.secondsPerPixel >= 1.0)
        resStr = juce::String(config.secondsPerPixel, 0) + "s/px";
    else
        resStr = juce::String(static_cast<int>(config.secondsPerPixel * 1000)) + "ms/px";
    
    juce::String infoStr = "X: " + timeStr + " | Y: " + lufsStr + " | " + 
                           lodStr + " (" + resStr + ")";
    
    g.setFont(10.0f);
    g.setColour(textColour.withAlpha(0.6f));
    
    int textWidth = 350;
    g.drawText(infoStr, bounds.getWidth() - textWidth - 10, 10, textWidth, 14,
               juce::Justification::right);
}

void LoudnessHistoryDisplay::resized()
{
    // Image strips maintain fixed resolution, only display scaling changes
}

void LoudnessHistoryDisplay::mouseWheelMove(const juce::MouseEvent& event,
                                             const juce::MouseWheelDetails& wheel)
{
    const float zoomFactor = 1.15f;
    
    if (event.mods.isShiftDown())
    {
        // Y-axis zoom (just changes view, not image)
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
        
        // Clamp to valid range
        if (viewMaxLufs > 0.0f)
        {
            viewMaxLufs = 0.0f;
            viewMinLufs = -newRange;
        }
        if (viewMinLufs < kMinLufs)
        {
            viewMinLufs = kMinLufs;
            viewMaxLufs = viewMinLufs + newRange;
        }
    }
    else
    {
        // X-axis zoom
        double newRange = viewTimeRange;
        if (wheel.deltaY > 0)
            newRange = viewTimeRange / zoomFactor;
        else if (wheel.deltaY < 0)
            newRange = viewTimeRange * zoomFactor;
        
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
    
    // Only allow Y-axis panning for now (X panning will come later with full history navigation)
    float lufsRange = viewMaxLufs - viewMinLufs;
    float lufsDelta = dy * lufsRange / static_cast<float>(getHeight());
    
    float newMin = viewMinLufs + lufsDelta;
    float newMax = viewMaxLufs + lufsDelta;
    
    // Clamp to valid range
    if (newMax > 0.0f)
    {
        newMax = 0.0f;
        newMin = newMax - lufsRange;
    }
    if (newMin < kMinLufs)
    {
        newMin = kMinLufs;
        newMax = newMin + lufsRange;
    }
    
    viewMinLufs = newMin;
    viewMaxLufs = newMax;
    
    repaint();
}

void LoudnessHistoryDisplay::mouseUp(const juce::MouseEvent&)
{
    isDragging = false;
}