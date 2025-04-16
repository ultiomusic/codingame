#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>

using namespace std;

int main() {
    string lineA, lineB;
    getline(cin, lineA);
    getline(cin, lineB);

    vector<long long> compA;
    {
        istringstream iss(lineA);
        long long number;
        while (iss >> number) {
            compA.push_back(number);
        }
    }

    vector<long long> compB;
    {
        istringstream iss(lineB);
        long long number;
        while (iss >> number) {
            compB.push_back(number);
        }
    }

    int segA = 0, segB = 0;
    long long dot = 0;

    long long remA = compA[segA * 2];
    long long remB = compB[segB * 2];

    while (segA < compA.size() / 2 && segB < compB.size() / 2) {
        long long common = min(remA, remB);
        dot += common * (compA[segA * 2 + 1] * compB[segB * 2 + 1]);

        remA -= common;
        remB -= common;

        if (remA == 0) {
            segA++;
            if (segA < compA.size() / 2) {
                remA = compA[segA * 2];
            }
        }
        if (remB == 0) {
            segB++;
            if (segB < compB.size() / 2) {
                remB = compB[segB * 2];
            }
        }
    }

    cout << dot << endl;
    return 0;
}
