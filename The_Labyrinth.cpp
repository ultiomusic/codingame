#include <iostream>
#include <vector>
#include <queue>
#include <string>
#include <algorithm>
#include <limits>
using namespace std;

const int dx[4] = {-1, 1, 0, 0};
const int dy[4] = {0, 0, -1, 1};
const string dirs[4] = {"UP", "DOWN", "LEFT", "RIGHT"};

vector<pair<int,int>> bfsPath(const vector<string>& maze, int R, int C, int sx, int sy, int tx, int ty) {
    vector<vector<bool>> visited(R, vector<bool>(C, false));
    vector<vector<pair<int,int>>> prev(R, vector<pair<int,int>>(C, {-1, -1}));
    queue<pair<int,int>> q;
    visited[sx][sy] = true;
    q.push({sx, sy});

    while (!q.empty()) {
        auto cur = q.front();
        q.pop();
        if (cur.first == tx && cur.second == ty) break;
        for (int d = 0; d < 4; d++) {
            int nx = cur.first + dx[d], ny = cur.second + dy[d];
            if (nx < 0 || nx >= R || ny < 0 || ny >= C)
                continue;
            if (!visited[nx][ny] && maze[nx][ny] != '#' && maze[nx][ny] != '?') {
                visited[nx][ny] = true;
                prev[nx][ny] = cur;
                q.push({nx, ny});
            }
        }
    }

    vector<pair<int,int>> path;
    if (!visited[tx][ty])
        return path;

    pair<int,int> cur = {tx, ty};
    while (!(cur.first == sx && cur.second == sy)) {
        path.push_back(cur);
        cur = prev[cur.first][cur.second];
    }
    reverse(path.begin(), path.end());
    return path;
}

pair<int,int> getFrontierTarget(const vector<string>& maze, int R, int C, int kr, int kc) {
    pair<int,int> best = {-1, -1};
    int bestDist = numeric_limits<int>::max();

    for (int i = 0; i < R; i++) {
        for (int j = 0; j < C; j++) {
            if (maze[i][j] == '#' || maze[i][j] == '?')
                continue;
            bool isFrontier = false;
            for (int d = 0; d < 4; d++) {
                int ni = i + dx[d], nj = j + dy[d];
                if (ni >= 0 && ni < R && nj >= 0 && nj < C) {
                    if (maze[ni][nj] == '?') {
                        isFrontier = true;
                        break;
                    }
                }
            }
            if (isFrontier) {
                auto path = bfsPath(maze, R, C, kr, kc, i, j);
                if (!path.empty()) {
                    int d = path.size();
                    if (d < bestDist) {
                        bestDist = d;
                        best = {i, j};
                    }
                }
            }
        }
    }
    if (best.first == -1)
        return {kr, kc};
    return best;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    int R, C, A;
    cin >> R >> C >> A;
    cin.ignore();
    
    vector<string> globalMaze(R, string(C, '?'));

    int startR = -1, startC = -1;
    int controlR = -1, controlC = -1;
    bool controlFound = false, alarmTriggered = false;

    pair<int,int> currentTarget = {-1, -1};
    string lastMove = "";

    while (true) {
        int kr, kc;
        cin >> kr >> kc;
        cin.ignore();
        vector<string> currentView;
        for (int i = 0; i < R; i++) {
            string row;
            cin >> row;
            cin.ignore();
            currentView.push_back(row);
        }

        for (int i = 0; i < R; i++) {
            for (int j = 0; j < C; j++) {
                if (currentView[i][j] != '?')
                    globalMaze[i][j] = currentView[i][j];
            }
        }

        for (int i = 0; i < R; i++) {
            for (int j = 0; j < C; j++) {
                if (currentView[i][j] == 'T') {
                    startR = i;
                    startC = j;
                }
                if (currentView[i][j] == 'C') {
                    controlR = i;
                    controlC = j;
                    controlFound = true;
                }
            }
        }

        if (kr == controlR && kc == controlC)
            alarmTriggered = true;

        if (alarmTriggered) {
            currentTarget = {startR, startC};
        }
        else if (controlFound) {
            currentTarget = {controlR, controlC};
        }
        else {

            if (currentTarget.first == -1 || 
               globalMaze[currentTarget.first][currentTarget.second] == '?' || 
               globalMaze[currentTarget.first][currentTarget.second] == '#') {
                currentTarget = getFrontierTarget(globalMaze, R, C, kr, kc);
            } else {
                bool stillFrontier = false;
                for (int d = 0; d < 4; d++) {
                    int ni = currentTarget.first + dx[d], nj = currentTarget.second + dy[d];
                    if (ni >= 0 && ni < R && nj >= 0 && nj < C) {
                        if (globalMaze[ni][nj] == '?') {
                            stillFrontier = true;
                            break;
                        }
                    }
                }
                if (!stillFrontier)
                    currentTarget = getFrontierTarget(globalMaze, R, C, kr, kc);
            }
        }

        auto path = bfsPath(globalMaze, R, C, kr, kc, currentTarget.first, currentTarget.second);
        string move;
        if (!path.empty()) {
            int nextR = path[0].first;
            int nextC = path[0].second;
            if (nextR < kr)
                move = "UP";
            else if (nextR > kr)
                move = "DOWN";
            else if (nextC < kc)
                move = "LEFT";
            else if (nextC > kc)
                move = "RIGHT";
        }
        else {
            currentTarget = getFrontierTarget(globalMaze, R, C, kr, kc);
            path = bfsPath(globalMaze, R, C, kr, kc, currentTarget.first, currentTarget.second);
            if (!path.empty()){
                int nextR = path[0].first;
                int nextC = path[0].second;
                if (nextR < kr)
                    move = "UP";
                else if (nextR > kr)
                    move = "DOWN";
                else if (nextC < kc)
                    move = "LEFT";
                else if (nextC > kc)
                    move = "RIGHT";
            } else {
                for (int d = 0; d < 4; d++) {
                    int nx = kr + dx[d], ny = kc + dy[d];
                    if (nx >= 0 && nx < R && ny >= 0 && ny < C &&
                        globalMaze[nx][ny] != '#' && globalMaze[nx][ny] != '?') {
                        move = dirs[d];
                        break;
                    }
                }
                if (move == "")
                    move = "RIGHT";
            }
        }

        lastMove = move;
        cout << move << endl;
    }

    return 0;
}