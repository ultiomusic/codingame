#include <stdio.h>
#include <stdlib.h>

#define MAX_NODES 500
#define MAX_ADJ MAX_NODES

typedef struct {
    int adj[MAX_ADJ];
    int adjCount;
    int exits[MAX_ADJ];
    int exitCount;
    int isExit;
} Vertex;

typedef struct {
    int first;
    int second;
} Pair;

Pair searchPath(Vertex graph[], int total, int start) {
    int visited[MAX_NODES] = {0};
    int queue[MAX_NODES];
    int front = 0, rear = 0;
    queue[rear++] = start;
    visited[start] = 1;
    int sel = -1;
    while (front < rear) {
        int cur = queue[front++];
        Vertex v = graph[cur];
        if (v.exitCount > 1) {
            sel = cur;
            break;
        } else if (v.exitCount == 1) {
            if (sel == -1) {
                sel = cur;
                if (cur == start)
                    break;
            }
            for (int i = 0; i < v.adjCount; i++) {
                int nb = v.adj[i];
                if (!visited[nb]) {
                    visited[nb] = 1;
                    queue[rear++] = nb;
                }
            }
        } else if (sel == -1) {
            for (int i = 0; i < v.adjCount; i++) {
                int nb = v.adj[i];
                if (!visited[nb]) {
                    visited[nb] = 1;
                    queue[rear++] = nb;
                }
            }
        }
    }
    Pair res;
    res.first = sel;
    res.second = graph[sel].exits[0];
    return res;
}

void removeElement(int arr[], int *cnt, int val) {
    int i;
    for (i = 0; i < *cnt; i++) {
        if (arr[i] == val)
            break;
    }
    if (i < *cnt) {
        for (; i < (*cnt) - 1; i++)
            arr[i] = arr[i+1];
        (*cnt)--;
    }
}

int main() {
    int totalNodes, totalLinks, exitTotal;
    scanf("%d %d %d", &totalNodes, &totalLinks, &exitTotal);
    Vertex graph[MAX_NODES];
    for (int i = 0; i < totalNodes; i++) {
        graph[i].adjCount = 0;
        graph[i].exitCount = 0;
        graph[i].isExit = 0;
    }
    for (int i = 0; i < totalLinks; i++) {
        int a, b;
        scanf("%d %d", &a, &b);
        graph[a].adj[graph[a].adjCount++] = b;
        graph[b].adj[graph[b].adjCount++] = a;
    }
    for (int i = 0; i < exitTotal; i++) {
        int ex;
        scanf("%d", &ex);
        graph[ex].isExit = 1;
        for (int j = 0; j < graph[ex].adjCount; j++) {
            int nb = graph[ex].adj[j];
            graph[nb].exits[graph[nb].exitCount++] = ex;
        }
    }
    while (1) {
        int agent;
        scanf("%d", &agent);
        Pair cut = searchPath(graph, totalNodes, agent);
        printf("%d %d\n", cut.first, cut.second);
        fflush(stdout);
        removeElement(graph[cut.first].exits, &graph[cut.first].exitCount, cut.second);
        removeElement(graph[cut.first].adj, &graph[cut.first].adjCount, cut.second);
        removeElement(graph[cut.second].adj, &graph[cut.second].adjCount, cut.first);
    }
    return 0;
}
