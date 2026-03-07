package ru.nsu.datagen.plancheck.struct;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.util.Collections;
import java.util.List;

public class PlanTree {
    public PlanNode root;
    public int nodeCount;

    public static PlanTree fromJson(String json) throws Exception {
        ObjectMapper mapper = new ObjectMapper();
        JsonNode rootNode = mapper.readTree(json);
        List<ExplainRoot> explainList;

        if (rootNode.isArray()) {
            // Если пришёл массив — десериализуем как список
            explainList = mapper.readValue(json, mapper.getTypeFactory()
                    .constructCollectionType(List.class, ExplainRoot.class));
        } else {
            // Если пришёл объект — оборачиваем в список
            ExplainRoot single = mapper.readValue(json, ExplainRoot.class);
            explainList = Collections.singletonList(single);
        }

        if (explainList.isEmpty()) {
            throw new IllegalArgumentException("Empty EXPLAIN JSON");
        }

        PlanNode rootNodePlan = explainList.get(0).plan;
        int count = countNodes(rootNodePlan);

        PlanTree tree = new PlanTree();
        tree.root = rootNodePlan;
        tree.nodeCount = count;
        return tree;
    }

    private static int countNodes(PlanNode node) {
        if (node == null) return 0;
        int count = 1;
        for (PlanNode child : node.getPlans()) {
            count += countNodes(child);
        }
        return count;
    }
}