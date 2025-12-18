/*
  ==============================================================================

    BatchRenderer.h
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class BatchRenderer : public Thread, public AsyncUpdater {
public:
  //==============================================================================
  struct RenderSettings {
    double sampleRate = 48000.0;
    int bitDepth = 24;
    double bpm = 120.0;
    float silenceThresholdDb = -50.0f;
  };

  struct RenderJob {
    File midiFile;
    int variationIndex = 0;
    String variationName;
    PluginDescription pluginDesc;
    MemoryBlock pluginState;
    int pitchOffset = 0;
    float velocityMultiplier = 1.0f;
    float volumeDb = 0.0f;
    File outputFile;
  };

  //==============================================================================
  BatchRenderer(ayra::PluginsManager &pm, const RenderSettings &settings,
                const File &outputDir);
  ~BatchRenderer() override;

  //==============================================================================
  void addJob(const RenderJob &job);
  void startRendering();
  void cancelRendering();

  //==============================================================================
  float getProgress() const;
  int getCompletedJobs() const { return completedCount.load(); }
  int getTotalJobs() const { return totalJobs; }
  bool isComplete() const { return completedCount.load() >= totalJobs; }

  //==============================================================================
  std::function<void()> onComplete;
  std::function<void(const String &error)> onError;
  std::function<void(float progress)> onProgress;

private:
  //==============================================================================
  void run() override;
  void handleAsyncUpdate() override;

  bool renderSingleJob(const RenderJob &job);

  //==============================================================================
  ayra::PluginsManager &pluginsManager;
  RenderSettings settings;
  File outputDirectory;

  CriticalSection jobLock;
  Array<RenderJob> pendingJobs;

  std::atomic<int> completedCount{0};
  std::atomic<int> failedCount{0};
  int totalJobs = 0;

  String lastError;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BatchRenderer)
};
