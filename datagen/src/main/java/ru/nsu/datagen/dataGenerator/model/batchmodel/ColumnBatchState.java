package ru.nsu.datagen.dataGenerator.model.batchmodel;

import java.util.List;

import lombok.Getter;
import ru.nsu.datagen.dataGenerator.generators.ColumnGenerator;
import ru.nsu.datagen.dataGenerator.generators.normal.NormalValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

/**
 *
 * @author kubicl
 */
public class ColumnBatchState {
    private final ColumnGenerator valueGenerator;
    @Getter private final List<ColumnMetadata> columns;
    @Getter private StateData curStateData;
    private StateData prevStateData;

    public ColumnBatchState(NormalValueGenerator valueGenerator, ColumnMetadata columnMetadata) {
        this.valueGenerator = valueGenerator;
        this.columns = List.of(columnMetadata);
        this.curStateData = new StateData(columnMetadata);
        this.prevStateData = null;
    }

    public ColumnBatchState(UniqueKeyGenerator valueGenerator, List<ColumnMetadata> compositePeers) {
        this.valueGenerator = valueGenerator;
        this.columns = compositePeers;
        this.curStateData = new StateData(compositePeers);
        this.prevStateData = null;
    }

    public ColumnBatchState(ColumnGenerator valueGenerator, ColumnMetadata columnMetadata) {
        this.valueGenerator = valueGenerator;
        this.columns = List.of(columnMetadata);
        this.curStateData = new StateData(columnMetadata);
        this.prevStateData = null;
    }

    public ColumnBatchState(ColumnGenerator valueGenerator, List<ColumnMetadata> columns) {
        this.valueGenerator = valueGenerator;
        this.columns = columns;
        this.curStateData = new StateData(columns);
        this.prevStateData = null;
    }

    public void rollbackToPreviousState() {
        if (this.prevStateData != null) {
            this.curStateData = this.prevStateData;
        }
    }

    public List<List<Object>> produceBatch(int batchSize) {
        return produceBatch(batchSize, 0);
    }

    public List<List<Object>> produceBatch(int batchSize, int emptyBatchCount) {
        this.prevStateData = new StateData(this.curStateData);
        this.curStateData.setEmptyBatchCount(emptyBatchCount);
        return valueGenerator.generateValues(batchSize, curStateData);
    }

    public List<List<Object>> regenerateRows(int count) {
        if (valueGenerator instanceof UniqueKeyGenerator ukg) {
            return ukg.regenerateRows(count);
        }
        return List.of();
    }
}
