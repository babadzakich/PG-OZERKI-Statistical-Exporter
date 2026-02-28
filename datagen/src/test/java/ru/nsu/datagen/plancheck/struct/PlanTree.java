package ru.nsu.datagen.plancheck.struct;

import com.fasterxml.jackson.databind.ObjectMapper;

import java.util.List;

public class PlanTree {
    public PlanNode root;
    public int nodeCount;

    public static PlanTree fromJson(String json) throws Exception {
        ObjectMapper mapper = new ObjectMapper();

        List<ExplainRoot> explainList = mapper.readValue(json, mapper.getTypeFactory()
                .constructCollectionType(List.class, ExplainRoot.class));

        if (explainList.isEmpty()) {
            throw new IllegalArgumentException("Empty EXPLAIN JSON");
        }

        PlanNode rootNode = explainList.get(0).plan;
        int count = countNodes(rootNode);

        PlanTree tree = new PlanTree();
        tree.root = rootNode;
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