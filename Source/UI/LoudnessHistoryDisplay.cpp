#include "LoudnessHistoryDisplay.h"
#include <cmath>
#include <algorithm>

LoudnessHistoryDisplay::LoudnessHistoryDisplay(LoudnessDataStore& store)
    : dataStore(store)
{
    setOpaque(true);
    initializeBitmaps();
    startTimerHz(60); // 60 Hz for smooth display
}

LoudnessHistoryDisplay::~LoudnessHistoryDisplay()
{
    stopTimer();
}

void LoudnessHistoryDisplay::initializeBitmaps()
{
    for (int lod = 0; lod < kNumLodLevels; ++lod)
    {
        const auto& config = kLodConfigs[static_cast<size_t>(lod)];
        auto& strip = bitmapStrips[static_cast<size_t>(lod)];
        
        strip.image = juce::Image(juce::Image::ARGB, config.imageWidth, kImageHeight, true);
        strip.image.clear(strip.image.getBounds(), backgroundColour);
        strip.lastRenderedTime = 0.0;
        strip.totalColumnsRendered = 0;
    }
}

void LoudnessHistoryDisplay::timerCallback()
{
    updateBitmaps();
    repaint();
}

void LoudnessHistoryDisplay::updateBitmaps()
{
    double currentDataTime = dataStore.getCurrentTime();
    
    // We render up to (currentDataTime - delay)
    // This gives us time to accumulate data for smooth curves
    double renderUpToTime = currentDataTime - kDisplayDelaySeconds;
    
    if (renderUpToTime <= 0.0)
        return;
    
    // Update each LOD level
    for (int lod = 0; lod < kNumLodLevels; ++lod)
    {
        const auto& config = kLodConfigs[static_cast<size_t>(lod)];
        auto& strip = bitmapStrips[static_cast<size_t>(lod)];
        
        // Calculate how many chunk widths we need to render
        double chunkDuration = config.secondsPerPixel * config.chunkSizePixels;
        
        while (strip.lastRenderedTime + chunkDuration <= renderUpToTime)
        {
            double chunkStartTime = strip.lastRenderedTime;
            double chunkEndTime = chunkStartTime + chunkDuration;
            
            // Clear the region we're about to draw
            int startCol = strip.totalColumnsRendered % config.imageWidth;
            clearBitmapRegion(lod, startCol, config.chunkSizePixels);
            
            // Render the curve chunk
            renderCurveChunkToBitmap(lod, chunkStartTime, chunkEndTime);
            
            strip.lastRenderedTime = chunkEndTime;
            strip.totalColumnsRendered += config.chunkSizePixels;
        }
    }
}

void LoudnessHistoryDisplay::clearBitmapRegion(int lodLevel, int startColumn, int numColumns)
{
    const auto& config = kLodConfigs[static_cast<size_t>(lodLevel)];
    auto& strip = bitmapStrips[static_cast<size_t>(lodLevel)];
    
    juce::Graphics g(strip.image);
    g.setColour(backgroundColour);
    
    // Handle wrap-around
    if (startColumn + numColumns <= config.imageWidth)
    {
        g.fillRect(startColumn, 0, numColumns, kImageHeight);
    }
    else
    {
        int firstPart = config.imageWidth - startColumn;
        int secondPart = numColumns - firstPart;
        g.fillRect(startColumn, 0, firstPart, kImageHeight);
        g.fillRect(0, 0, secondPart, kImageHeight);
    }
}

void LoudnessHistoryDisplay::renderCurveChunkToBitmap(int lodLevel, double chunkStartTime, double chunkEndTime)
{
    const auto& config = kLodConfigs[static_cast<size_t>(lodLevel)];
    auto& strip = bitmapStrips[static_cast<size_t>(lodLevel)];
    
    // Get data points for this time range (with some overlap for curve continuity)
    double overlapTime = config.secondsPerPixel * 2;
    auto points = dataStore.getPointsInRange(chunkStartTime - overlapTime, chunkEndTime + overlapTime);
    
    if (points.empty())
        return;
    
    juce::Graphics g(strip.image);
    
    // Group points by pixel column and find min/max
    struct ColumnData
    {
        float momentaryMin{100.0f}, momentaryMax{-100.0f};
        float shortTermMin{100.0f}, shortTermMax{-100.0f};
        bool hasData{false};
    };
    
    // We need data for the chunk plus overlap for curve continuity
    int numColumnsTotal = config.chunkSizePixels + 4; // Extra for overlap
    std::vector<ColumnData> columns(static_cast<size_t>(numColumnsTotal));
    
    double pixelStartTime = chunkStartTime - config.secondsPerPixel * 2;
    
    for (const auto& point : points)
    {
        int col = static_cast<int>((point.timestamp - pixelStartTime) / config.secondsPerPixel);
        
        if (col >= 0 && col < numColumnsTotal)
        {
            auto& cd = columns[static_cast<size_t>(col)];
            
            if (point.momentary > -100.0f)
            {
                cd.momentaryMin = std::min(cd.momentaryMin, point.momentary);
                cd.momentaryMax = std::max(cd.momentaryMax, point.momentary);
                cd.hasData = true;
            }
            if (point.shortTerm > -100.0f)
            {
                cd.shortTermMin = std::min(cd.shortTermMin, point.shortTerm);
                cd.shortTermMax = std::max(cd.shortTermMax, point.shortTerm);
                cd.hasData = true;
            }
        }
    }
    
    // Build paths for the curves
    // We draw onto the bitmap at the correct column positions
    
    int baseColumn = strip.totalColumnsRendered % config.imageWidth;
    
    // Momentary fill path
    juce::Path momentaryFill;
    juce::Path momentaryLine;
    juce::Path shortTermFill;
    juce::Path shortTermLine;
    
    std::vector<juce::Point<float>> momentaryTopPts;
    std::vector<juce::Point<float>> momentaryBotPts;
    std::vector<juce::Point<float>> shortTermTopPts;
    std::vector<juce::Point<float>> shortTermBotPts;
    
    // Collect points for the chunk we're rendering (skip overlap at start)
    for (int i = 2; i < 2 + config.chunkSizePixels; ++i)
    {
        const auto& cd = columns[static_cast<size_t>(i)];
        
        if (!cd.hasData)
            continue;
        
        int imgCol = (baseColumn + i - 2) % config.imageWidth;
        float x = static_cast<float>(imgCol);
        
        if (cd.momentaryMax > -100.0f)
        {
            float yTop = lufsToImageY(cd.momentaryMax);
            float yBot = lufsToImageY(cd.momentaryMin);
            float yMid = (yTop + yBot) * 0.5f;
            
            momentaryTopPts.push_back({x, yTop});
            momentaryBotPts.push_back({x, yBot});
            
            if (momentaryLine.isEmpty())
                momentaryLine.startNewSubPath(x, yMid);
            else
                momentaryLine.lineTo(x, yMid);
        }
        
        if (cd.shortTermMax > -100.0f)
        {
            float yTop = lufsToImageY(cd.shortTermMax);
            float yBot = lufsToImageY(cd.shortTermMin);
            float yMid = (yTop + yBot) * 0.5f;
            
            shortTermTopPts.push_back({x, yTop});
            shortTermBotPts.push_back({x, yBot});
            
            if (shortTermLine.isEmpty())
                shortTermLine.startNewSubPath(x, yMid);
            else
                shortTermLine.lineTo(x, yMid);
        }
    }
    
    // Build fill paths
    if (momentaryTopPts.size() >= 2)
    {
        momentaryFill.startNewSubPath(momentaryTopPts[0]);
        for (size_t i = 1; i < momentaryTopPts.size(); ++i)
            momentaryFill.lineTo(momentaryTopPts[i]);
        for (auto it = momentaryBotPts.rbegin(); it != momentaryBotPts.rend(); ++it)
            momentaryFill.lineTo(*it);
        momentaryFill.closeSubPath();
    }
    
    if (shortTermTopPts.size() >= 2)
    {
        shortTermFill.startNewSubPath(shortTermTopPts[0]);
        for (size_t i = 1; i < shortTermTopPts.size(); ++i)
            shortTermFill.lineTo(shortTermTopPts[i]);
        for (auto it = shortTermBotPts.rbegin(); it != shortTermBotPts.rend(); ++it)
            shortTermFill.lineTo(*it);
        shortTermFill.closeSubPath();
    }
    
    // Draw momentary (behind)
    if (!momentaryFill.isEmpty())
    {
        g.setColour(momentaryColour.withAlpha(0.35f));
        g.fillPath(momentaryFill);
    }
    if (!momentaryLine.isEmpty())
    {
        g.setColour(momentaryColour);
        g.strokePath(momentaryLine, juce::PathStrokeType(1.2f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    // Draw short-term (on top)
    if (!shortTermFill.isEmpty())
    {
        g.setColour(shortTermColour.withAlpha(0.45f));
        g.fillPath(shortTermFill);
    }
    if (!shortTermLine.isEmpty())
    {
        g.setColour(shortTermColour);
        g.strokePath(shortTermLine, juce::PathStrokeType(1.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

float LoudnessHistoryDisplay::lufsToImageY(float lufs) const
{
    float normalized = (kMaxLufs - lufs) / kLufsRange;
    return juce::jlimit(0.0f, static_cast<float>(kImageHeight - 1), 
                        normalized * static_cast<float>(kImageHeight));
}

int LoudnessHistoryDisplay::timeToColumn(double time, int lodLevel) const
{
    const auto& config = kLodConfigs[static_cast<size_t>(lodLevel)];
    return static_cast<int>(time / config.secondsPerPixel);
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
    drawFromBitmap(g);
    drawGrid(g);
    drawCurrentValues(g);
    drawZoomInfo(g);
}

void LoudnessHistoryDisplay::drawBackground(juce::Graphics& g)
{
    g.fillAll(backgroundColour);
}

void LoudnessHistoryDisplay::drawFromBitmap(juce::Graphics& g)
{
    int displayWidth = getWidth();
    int displayHeight = getHeight();
    
    if (displayWidth <= 0 || displayHeight <= 0)
        return;
    
    double currentDataTime = dataStore.getCurrentTime();
    double displayTime = currentDataTime - kDisplayDelaySeconds;
    
    if (displayTime <= 0.0)
        return;
    
    // Select LOD level based on zoom
    currentLodLevel = getLodLevelForTimeRange(viewTimeRange);
    const auto& config = kLodConfigs[static_cast<size_t>(currentLodLevel)];
    const auto& strip = bitmapStrips[static_cast<size_t>(currentLodLevel)];
    
    // Calculate the display window
    double displayEndTime = displayTime;
    double displayStartTime = displayEndTime - viewTimeRange;
    
    // Convert to pixel positions in the bitmap
    double endPixelExact = displayEndTime / config.secondsPerPixel;
    double startPixelExact = displayStartTime / config.secondsPerPixel;
    
    // Sub-pixel offset for smooth scrolling
    float subPixelOffset = static_cast<float>(endPixelExact - std::floor(endPixelExact));
    
    int endPixel = static_cast<int>(std::floor(endPixelExact));
    int startPixel = static_cast<int>(std::floor(startPixelExact));
    int numPixels = endPixel - startPixel + 1;
    
    if (numPixels <= 0)
        return;
    
    // Calculate source region in the ring buffer bitmap
    int srcEndCol = endPixel % config.imageWidth;
    int srcStartCol = startPixel % config.imageWidth;
    
    // Calculate Y region based on view
    float srcYTop = (kMaxLufs - viewMaxLufs) / kLufsRange * static_cast<float>(kImageHeight);
    float srcYBot = (kMaxLufs - viewMinLufs) / kLufsRange * static_cast<float>(kImageHeight);
    
    srcYTop = juce::jlimit(0.0f, static_cast<float>(kImageHeight), srcYTop);
    srcYBot = juce::jlimit(0.0f, static_cast<float>(kImageHeight), srcYBot);
    
    int srcY = static_cast<int>(srcYTop);
    int srcH = static_cast<int>(srcYBot - srcYTop);
    
    if (srcH <= 0)
        return;
    
    // Apply sub-pixel offset for smooth scrolling
    float destOffsetX = -subPixelOffset * (static_cast<float>(displayWidth) / static_cast<float>(numPixels));
    
    // Handle ring buffer wrap-around
    if (srcStartCol <= srcEndCol)
    {
        // Simple case: contiguous region
        int srcW = srcEndCol - srcStartCol + 1;
        
        g.drawImage(strip.image,
                    static_cast<int>(destOffsetX), 0, 
                    displayWidth + static_cast<int>(std::abs(destOffsetX)) + 1, displayHeight,
                    srcStartCol, srcY, srcW, srcH);
    }
    else
    {
        // Wrapped case: need to draw two parts
        int part1Width = config.imageWidth - srcStartCol;
        int part2Width = srcEndCol + 1;
        int totalWidth = part1Width + part2Width;
        
        float scale = static_cast<float>(displayWidth) / static_cast<float>(totalWidth);
        int destPart1Width = static_cast<int>(part1Width * scale);
        int destPart2Width = displayWidth - destPart1Width;
        
        // Part 1 (end of ring buffer)
        g.drawImage(strip.image,
                    static_cast<int>(destOffsetX), 0, destPart1Width, displayHeight,
                    srcStartCol, srcY, part1Width, srcH);
        
        // Part 2 (start of ring buffer)
        g.drawImage(strip.image,
                    static_cast<int>(destOffsetX) + destPart1Width, 0, destPart2Width, displayHeight,
                    0, srcY, part2Width, srcH);
    }
}

void LoudnessHistoryDisplay::drawGrid(juce::Graphics& g)
{
    int width = getWidth();
    int height = getHeight();
    
    double currentDataTime = dataStore.getCurrentTime();
    double displayTime = currentDataTime - kDisplayDelaySeconds;
    double displayStartTime = displayTime - viewTimeRange;
    
    // Horizontal grid lines (LUFS)
    float lufsRange = viewMaxLufs - viewMinLufs;
    float gridStep = 6.0f;
    if (lufsRange > 40.0f) gridStep = 12.0f;
    if (lufsRange < 20.0f) gridStep = 3.0f;
    
    float startLufs = std::ceil(viewMinLufs / gridStep) * gridStep;
    
    g.setFont(10.0f);
    for (float lufs = startLufs; lufs <= viewMaxLufs; lufs += gridStep)
    {
        float normY = (viewMaxLufs - lufs) / lufsRange;
        int y = static_cast<int>(normY * height);
        
        g.setColour(gridColour);
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(width));
        
        g.setColour(textColour.withAlpha(0.7f));
        g.drawText(juce::String(static_cast<int>(lufs)) + " LUFS",
                   5, y - 12, 60, 12, juce::Justification::left);
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
    
    double gridStartTime = std::floor(displayStartTime / timeStep) * timeStep;
    
    for (double t = gridStartTime; t <= displayTime + timeStep; t += timeStep)
    {
        if (t < displayStartTime) continue;
        
        float normX = static_cast<float>((t - displayStartTime) / viewTimeRange);
        int x = static_cast<int>(normX * width);
        
        if (x < 0 || x > width) continue;
        
        g.setColour(gridColour);
        g.drawVerticalLine(x, 0.0f, static_cast<float>(height));
        
        g.setColour(textColour.withAlpha(0.7f));
        
        juce::String label;
        if (t >= 3600.0)
        {
            int h = static_cast<int>(t) / 3600;
            int m = (static_cast<int>(t) % 3600) / 60;
            int s = static_cast<int>(t) % 60;
            label = juce::String::formatted("%d:%02d:%02d", h, m, s);
        }
        else if (t >= 60.0)
        {
            int m = static_cast<int>(t) / 60;
            int s = static_cast<int>(t) % 60;
            label = juce::String::formatted("%d:%02d", m, s);
        }
        else if (timeStep >= 1.0)
        {
            label = juce::String(static_cast<int>(t)) + "s";
        }
        else
        {
            label = juce::String(t, 1) + "s";
        }
        
        g.drawText(label, x - 30, height - 15, 60, 12, juce::Justification::centred);
    }
}

void LoudnessHistoryDisplay::drawCurrentValues(juce::Graphics& g)
{
    int boxWidth = 120;
    int boxHeight = 40;
    int margin = 10;
    
    // Momentary box
    juce::Rectangle<int> mBox(margin, margin, boxWidth, boxHeight);
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
    juce::Rectangle<int> sBox(margin + boxWidth + margin, margin, boxWidth, boxHeight);
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
    auto bounds = getLocalBounds();
    
    juce::String timeStr;
    if (viewTimeRange >= 3600.0)
        timeStr = juce::String(viewTimeRange / 3600.0, 2) + " hrs";
    else if (viewTimeRange >= 60.0)
        timeStr = juce::String(viewTimeRange / 60.0, 1) + " min";
    else
        timeStr = juce::String(viewTimeRange, 1) + " sec";
    
    float lufsRange = viewMaxLufs - viewMinLufs;
    juce::String lufsStr = juce::String(static_cast<int>(lufsRange)) + " dB";
    
    juce::String lodStr = "LOD " + juce::String(currentLodLevel);
    
    const auto& config = kLodConfigs[static_cast<size_t>(currentLodLevel)];
    juce::String resStr;
    if (config.secondsPerPixel >= 1.0)
        resStr = juce::String(static_cast<int>(config.secondsPerPixel)) + "s/px";
    else
        resStr = juce::String(static_cast<int>(config.secondsPerPixel * 1000.0)) + "ms/px";
    
    juce::String info = "X: " + timeStr + " | Y: " + lufsStr + " | " + lodStr + " (" + resStr + ")";
    
    g.setFont(10.0f);
    g.setColour(textColour.withAlpha(0.6f));
    g.drawText(info, bounds.getWidth() - 360, 10, 350, 14, juce::Justification::right);
}

void LoudnessHistoryDisplay::resized()
{
    // Bitmap resolution stays fixed; display just scales
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
        
        if (viewMaxLufs > 0.0f) { viewMaxLufs = 0.0f; viewMinLufs = -newRange; }
        if (viewMinLufs < kMinLufs) { viewMinLufs = kMinLufs; viewMaxLufs = kMinLufs + newRange; }
    }
    else
    {
        // X zoom
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
    float lufsDelta = dy * lufsRange / static_cast<float>(getHeight());
    
    float newMin = viewMinLufs + lufsDelta;
    float newMax = viewMaxLufs + lufsDelta;
    
    if (newMax > 0.0f) { newMax = 0.0f; newMin = -lufsRange; }
    if (newMin < kMinLufs) { newMin = kMinLufs; newMax = kMinLufs + lufsRange; }
    
    viewMinLufs = newMin;
    viewMaxLufs = newMax;
    
    repaint();
}

void LoudnessHistoryDisplay::mouseUp(const juce::MouseEvent&)
{
    isDragging = false;
}