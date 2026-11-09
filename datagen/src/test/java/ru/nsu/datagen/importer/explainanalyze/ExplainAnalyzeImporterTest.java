package ru.nsu.datagen.importer.explainanalyze;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import ru.nsu.datagen.dataGenerator.model.AnalyzePlanInfo;
import java.nio.file.Files;
import java.nio.file.Path;
import static org.junit.jupiter.api.Assertions.*;

class ExplainAnalyzeImporterTest {
    @TempDir Path temp;

    @Test void conjunctionProducesPerAtomLowerBoundsAndLoopAdjustedCounts() throws Exception {
        String json = "[{\"Plan\":{\"Node Type\":\"Seq Scan\",\"Relation Name\":\"flights\",\"Actual Rows\":4,\"Actual Loops\":3,\"Rows Removed by Filter\":6,\"Filter\":\"(status = 'Arrived'::text) AND (delay > 10)\"}}]";
        AnalyzePlanInfo info = importOne(json);
        assertEquals(12, info.getActualRows());
        assertEquals(30, info.getTotalRows());
        assertEquals(2, info.getPredicateObservations().size());
        assertTrue(info.getPredicateObservations().stream().allMatch(o -> o.isLowerBound() && o.getMatchingRows() == 12 && o.getInputRows() == 30));
    }

    @Test void disjunctionDoesNotCreateIndividualObservations() throws Exception {
        String json = "{\"Plan\":{\"Node Type\":\"Seq Scan\",\"Relation Name\":\"flights\",\"Actual Rows\":4,\"Rows Removed by Filter\":6,\"Filter\":\"status = 'Arrived' OR status = 'Departed'\"}}";
        assertTrue(importOne(json).getPredicateObservations().isEmpty());
    }

    @Test void missingRuntimeCountsDoNotCreateEvidence() throws Exception {
        String json = "{\"Plan\":{\"Node Type\":\"Seq Scan\",\"Relation Name\":\"flights\",\"Filter\":\"status = 'Arrived'\"}}";
        assertTrue(importOne(json).getPredicateObservations().isEmpty());
    }

    private AnalyzePlanInfo importOne(String json) throws Exception {
        Path file = temp.resolve("plan.json"); Files.writeString(file, json);
        return ExplainAnalyzeImporter.importExplainAnalyze(file.toString()).get("flights").get(0);
    }
}
