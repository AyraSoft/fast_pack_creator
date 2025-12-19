/*
  ==============================================================================

    ParallelBatchRenderer.h
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

    Parallel rendering with ayra_rapid_thread_pool.
    Key constraint: One clip per row at a time (sequential per row, parallel
  across rows).

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <ayra_rapid_thread_pool/ayra_rapid_thread_pool.h>

//==============================================================================
class ParallelBatchRenderer : public Timer {
public:
  //==============================================================================
  struct RenderSettings {
    double sampleRate = 48000.0;
    int bitDepth = 24;
    double bpm = 120.0;
    float silenceThresholdDb = -50.0f;
    float masterGainDb = 0.0f; // Master gain in dB to apply to all renders
    bool loop =
        false; // If true, truncate exactly at MIDI end for seamless looping
    bool normalize = false; // If true, apply LUFS normalization via FFmpeg
    double normalizationLufs = -12.0; // Target LUFS level
  };

  struct RenderJob {
    int rowIndex = 0;
    int columnIndex = 0;
    File midiFile;
    String variationName;
    PluginDescription pluginDesc;
    MemoryBlock pluginState;
    int pitchOffset = 0;
    float velocityMultiplier = 1.0f;
    float volumeDb = 0.0f;
    double bpm = 120.0; // BPM for this specific job (for tempo-synced plugins)
    File outputFile;
  };

  //==============================================================================
  ParallelBatchRenderer(ayra::PluginsManager &pm,
                        const RenderSettings &settings, const File &outputDir);
  ~ParallelBatchRenderer() override;

  //==============================================================================
  // Add a job. Jobs for the same row will be processed sequentially.
  void addJob(const RenderJob &job);

  // Start rendering all queued jobs
  void startRendering();

  // Cancel all pending jobs
  void cancelRendering();

  //==============================================================================
  float getProgress() const;
  int getCompletedJobs() const { return completedCount.load(); }
  int getTotalJobs() const { return totalJobs; }
  bool isComplete() const { return completedCount.load() >= totalJobs; }
  bool isRendering() const { return rendering.load(); }

  //==============================================================================
  std::function<void()> onComplete;
  std::function<void(const String &error)> onError;
  std::function<void(float progress)> onProgress;

  // Warning flags
  bool wasFfmpegMissing() const { return ffmpegMissing.load(); }

  // Get list of problematic files (empty, corrupt, or silent)
  StringArray getProblematicFiles() const {
    ScopedLock sl(problemFilesLock);
    return problematicFiles;
  }

private:
  //==============================================================================
  // Queue for each row - jobs are processed sequentially within a row
  struct RowQueue {
    int rowIndex = 0;
    std::queue<RenderJob> jobs;
    std::atomic<bool> isProcessing{false};
  };

  //==============================================================================
  void timerCallback() override;
  void processNextJobForRow(int rowIndex);
  bool renderSingleJob(const RenderJob &job);
  void onJobCompleted(int rowIndex, bool success, const String &error);

  //==============================================================================
  ayra::PluginsManager &pluginsManager;
  RenderSettings settings;
  File outputDirectory;

  ayra::RapidThreadPool threadPool;

  CriticalSection queueLock;
  OwnedArray<RowQueue> rowQueues;

  std::atomic<int> completedCount{0};
  std::atomic<int> failedCount{0};
  std::atomic<bool> rendering{false};
  std::atomic<bool> cancelled{false};
  std::atomic<bool> ffmpegMissing{
      false}; // Set if normalization requested but FFmpeg not found

  int totalJobs = 0;
  String lastError;

  // Track problematic files
  mutable CriticalSection problemFilesLock;
  StringArray problematicFiles;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParallelBatchRenderer)
};
