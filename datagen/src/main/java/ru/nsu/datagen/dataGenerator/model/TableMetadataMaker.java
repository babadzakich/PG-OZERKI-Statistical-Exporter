package ru.nsu.datagen.dataGenerator.model;

import ru.nsu.datagen.dataGenerator.generators.fk.RelationshipType;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.IntStream;

public class TableMetadataMaker {
    //private Map<String, List<String[]>> columnDataGroupedByTablename;
   //private Map<String, List<String>> tableToColumnNames;

//    public void TableMetadataMaker(List<String[]> rawData) {
//        columnDataGroupedByTablename = new HashMap<>();
//
//        for(String[] line : rawData) {
//            if (!columnDataGroupedByTablename.containsKey(line[1])) {
//                columnDataGroupedByTablename.put(line[1], new ArrayList<>());
//            }
//            columnDataGroupedByTablename.get(line[1]).add(line);
//        }
//        processRawTableMetadata();
//    }

    public static List<TableMetadata> processRawTableMetadata(List<String[]> rawData) {
        Map<String, List<ColumnMetadata>> columnDataGroupedByTablename = new HashMap<>();

        for (String[] line : rawData) {
            ForeignKeyMetadata fkMetadata = line[6].contains("FK")
                    ? new ForeignKeyMetadata(line[9], line[10], line[9].equals("NULL") ? null : RelationshipType.valueOf(line[8]))
                    : null;
            int recordCountValue = Integer.parseInt(line[4]) == -1 ? 0 : Integer.parseInt(line[4]);
            double nullPercentageValue = (line[5] == null || line[5].isEmpty()) ? 0.0 : Double.parseDouble(line[5]);
            ColumnMetadata columnMetadata = ColumnMetadata.builder()
                .name(line[2])
                .dataType(line[3])
                
                .isPrimaryKey(line[6].contains("PK"))
                .isForeignKey(line[6].contains("FK"))
                .isUnique(line[6].contains("UNIQUE") || line[6].contains("PK")) // Лучше проверять на "UNIQUE" ИЛИ "PK"
                
                .nullPercentage(nullPercentageValue)
                .recordCount(recordCountValue)
                .maxLength(line[7].equals("-1") ? null : Integer.parseInt(line[7])) // Обработка -1 для длины
                .avgTupleSize(Integer.parseInt(line[13]))
                
                .foreignKeyMetadata(fkMetadata)
                .mvc(processMCV(line[11], line[12], recordCountValue))
                
                .build();
            if (!columnDataGroupedByTablename.containsKey(line[1])) {
                columnDataGroupedByTablename.put(line[1], new ArrayList<>());
            }
            columnDataGroupedByTablename.get(line[1]).add(columnMetadata);
        }

        List<TableMetadata> tableMetadataList = new ArrayList<>();
        for (String tableName : columnDataGroupedByTablename.keySet()) {
            Map<String, ColumnMetadata> columnMetadataMap = new HashMap<>();
            columnDataGroupedByTablename.get(tableName).forEach(
                    columnMetadata -> columnMetadataMap.put(columnMetadata.getName(), columnMetadata)
            );
            tableMetadataList.add(
                    new TableMetadata(
                            tableName,
                            columnMetadataMap,
                            columnDataGroupedByTablename.get(tableName).get(0).getRecordCount()
                    )
            );
        }

        return tableMetadataList;
    }

    private static Map<String, Integer> processMCV(String rawMCVArray, String rawMCFArray, int rowCount) {
        List<String> processedMCV = parsePgArrayString(rawMCVArray);
        List<String> processedMCF = parsePgArrayString(rawMCFArray);
        Map<String, Integer> resultDistribution = IntStream.range(0, processedMCV.size())
            .boxed() // Создаем поток Integer (индексов)
            .collect(Collectors.toMap(
                // Ключ: Значение MCV (берется из mcvList по индексу)
                processedMCV::get, 
                
                // Значение: Абсолютный счетчик (расчитывается из mcfList по индексу)
                index -> {
                    Double frequency = Double.parseDouble(processedMCF.get(index));
                    // Расчет абсолютного количества и округление до ближайшего целого
                    long absoluteCount = Math.round(frequency * rowCount);
                    return (int) absoluteCount; // Приведение к Integer
                }
            ));

        return resultDistribution;
    }

    /**
     * Парсит строку массива PostgreSQL (например, "{val1, "val 2", val3}") в список строк Java.
     */
    public static List<String> parsePgArrayString(String arrayString) {
        if (arrayString == null || arrayString.isEmpty() || "{}".equals(arrayString)) {
            return List.of(); // Возвращаем пустой список для пустых массивов
        }

        String cleanedString = arrayString.substring(1, arrayString.length() - 1);
        if (cleanedString.isEmpty()) {
            return List.of();
        }

        // 2. Регулярное выражение для разделения
        // Ищет элемент, который либо в кавычках ("...") либо не содержит запятых и кавычек
        Pattern pattern = Pattern.compile("(\"[^\"]*\"|[^,]+)");
        Matcher matcher = pattern.matcher(cleanedString);
        
        return matcher.results()
            .map(match -> match.group(1).trim()) // Получаем элемент и удаляем внешний пробел
            .map(element -> {
                // 3. Удаление внешних кавычек (для экранированных строк)
                if (element.startsWith("\"") && element.endsWith("\"")) {
                    // Если элемент в кавычках, удаляем их и, возможно, обрабатываем внутренние эскейп-последовательности
                    return element.substring(1, element.length() - 1).replace("\"\"", "\"");
                }
                return element;
            })
            .collect(Collectors.toList());
    }
}
