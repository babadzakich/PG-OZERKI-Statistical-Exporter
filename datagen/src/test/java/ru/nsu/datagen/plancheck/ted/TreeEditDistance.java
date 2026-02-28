package ru.nsu.datagen.plancheck.ted;

import ru.nsu.datagen.plancheck.struct.PlanNode;

import java.util.Collections;
import java.util.List;

/* TODO: склеить тип ноды с полем, отвечающим за имя отношения, параметры джойна и тд (Seq -> Seq(MyTable))
    тогда можно +- однозначно идентифицировать узлы и это буде относительно точно
 */

/** Zhang-Shasha impl
 учитывается только тип узла
 */
public class TreeEditDistance {
    float deleteCost = 1f;
    float relable = 1f;
    float insert = 1f;

    public static int compute(PlanNode tree1, PlanNode tree2) {
        if (tree1 == null && tree2 == null) return 0;
        if (tree1 == null) return costInsertTree(tree2);
        if (tree2 == null) return costDeleteTree(tree1);
        return forestDistance(Collections.singletonList(tree1), Collections.singletonList(tree2));
    }


    private static int costDeleteTree(PlanNode node) {
        int cost = 1;
        for (PlanNode child : safeGetPlans(node)) {
            cost += costDeleteTree(child);
        }
        return cost;
    }

    private static int costInsertTree(PlanNode node) {
        int cost = 1;
        for (PlanNode child : safeGetPlans(node)) {
            cost += costInsertTree(child);
        }
        return cost;
    }

    private static int costReplace(PlanNode a, PlanNode b) {
        return a.getFieldSum().equals(b.getFieldSum()) ? 0 : 1;
    }

    private static int forestDistance(List<PlanNode> forest1, List<PlanNode> forest2) {
        int n = forest1.size();
        int m = forest2.size();
        int[][] dp = new int[n + 1][m + 1];

        // Заполняем таблицу снизу вверх
        for (int i = n; i >= 0; i--) {
            for (int j = m; j >= 0; j--) {
                if (i == n && j == m) {
                    dp[i][j] = 0;
                } else if (i == n) {
                    // Вставка оставшихся из forest2
                    int cost = 0;
                    for (int k = j; k < m; k++) {
                        cost += costInsertTree(forest2.get(k));
                    }
                    dp[i][j] = cost;
                } else if (j == m) {
                    // Удаление оставшихся из forest1
                    int cost = 0;
                    for (int k = i; k < n; k++) {
                        cost += costDeleteTree(forest1.get(k));
                    }
                    dp[i][j] = cost;
                } else {
                    int deleteOption = dp[i + 1][j] + costDeleteTree(forest1.get(i));
                    int insertOption = dp[i][j + 1] + costInsertTree(forest2.get(j));
                    int childrenDist = forestDistance(
                            safeGetPlans(forest1.get(i)),
                            safeGetPlans(forest2.get(j))
                    );
                    int replaceOption = childrenDist
                            + costReplace(forest1.get(i), forest2.get(j))
                            + dp[i + 1][j + 1];

                    dp[i][j] = Math.min(deleteOption, Math.min(insertOption, replaceOption));
                }
            }
        }
        return dp[0][0];
    }

    private static List<PlanNode> safeGetPlans(PlanNode node) {
        if (node == null || node.plans == null) {
            return Collections.emptyList();
        }
        return node.plans;
    }
}