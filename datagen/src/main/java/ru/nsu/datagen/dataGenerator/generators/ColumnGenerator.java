package ru.nsu.datagen.dataGenerator.generators;

import java.util.List;

import ru.nsu.datagen.dataGenerator.model.batchmodel.StateData;

/**
 *
 * @author kubicl
 */
public interface ColumnGenerator {
    List<List<Object>> generateValues(int batchSize, StateData stateData);
}
