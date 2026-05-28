package ru.nsu.datagen.dataGenerator.generators;

import java.util.List;

import ru.nsu.datagen.dataGenerator.model.batchmodel.StateData;

/**
 * Базовый интерфейс для всех генераторов колонок.
 *
 * <p>Реализации возвращают данные в виде {@code List<List<Object>>}, где внешний список —
 * колонки, внутренний — значения строк этой колонки. Для одиночной колонки размер
 * внешнего списка равен 1; для составного ключа (Markov, composite FK) — числу колонок.
 */
public interface ColumnGenerator {
    /**
     * Генерирует следующий батч данных.
     *
     * @param batchSize  желаемое число строк в батче
     * @param stateData  состояние генерации: счётчики сгенерированных строк, шансы MCV/null/бакетов
     * @return список колонок; каждая колонка — список значений размером {@code <= batchSize}
     */
    List<List<Object>> generateValues(int batchSize, StateData stateData);
}
