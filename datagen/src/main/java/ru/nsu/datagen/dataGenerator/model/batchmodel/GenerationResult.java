package ru.nsu.datagen.dataGenerator.model.batchmodel;

import java.util.List;

/**
 *
 * @author kubicl
 */
public record GenerationResult(
    List<List<Object>> generatedValues,
    StateData nextStateData
) {}
