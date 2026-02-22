package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import ru.nsu.datagen.dataGenerator.generators.numbergenerator.binary.ByteaValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.datetime.*;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.geoma.PointValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.numeric.*;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class ValueGeneratorFactory {
    public static ValueGenerator createValueGenerator(ColumnMetadata columnMetadata) {
        String type = columnMetadata.getDataType();
        return switch (type.toLowerCase()) {
            case "smallint", "int2" -> new SmallintValueGenerator();
            case "integer", "int4", "int" -> new IntegerValueGenerator();
            case "bigint", "int8" -> new LongValueGenerator();
            case "real", "float4" -> new FloatValueGenerator();
            case "double precision", "float8" -> new DoubleValueGenerator();
            case "boolean" -> new BooleanValueGenerator();
            case "money" -> new MoneyValueGenerator();
            case "bytea" -> new ByteaValueGenerator(Math.max(columnMetadata.getAvgTupleSize() > 0 ? columnMetadata.getAvgTupleSize() : columnMetadata.getMaxLength(), 1));
            case "date" -> new DateValueGenerator();
            case "timestamp", "timestamp without time zone" -> new TimestampValueGenerator();
            case "timestamp with time zone", "timestamptz" -> new TimestampTZValueGenerator();
            case "time without time zone", "time" -> new TimeValueGenerator();
            case "time with time zone", "timetz" -> new TimeTZValueGenerator();
            case "interval" -> new IntervalValueGenerator();
//            case "tstzrange" -> new TstzrangeValueGenerator();
            case "point" -> new PointValueGenerator();
            case "jsonb", "json" -> new JSONValueGenerator();
            default -> {
                if (type.contains("numeric(") || type.contains("decimal(")) {
                    yield getNumeric(type);
                }
                yield new StringValueGenerator(columnMetadata.getAvgTupleSize(), columnMetadata.getMaxLength());
            }
        };
    }
    private static NumericValueGenerator getNumeric(String dataType) {
        Pattern pattern = Pattern.compile("(numeric|decimal)\\((\\d+)(?:,(\\d+))?\\)");
        Matcher matcher = pattern.matcher(dataType);

        int precision, scale;

        if (matcher.find()) {
            precision = Integer.parseInt(matcher.group(2));
            scale = matcher.group(3) != null ? Integer.parseInt(matcher.group(3)) : 0;
        } else {
            precision = 10;
            scale = 0;
        }

        return new NumericValueGenerator(precision, scale);
    }
}
