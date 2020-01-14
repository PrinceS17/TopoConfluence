import numpy as np

def find_cluster(raw_rate):
    # given the raw rate matrix: N x N, find the co-bottleneck, return [flow:co-btnk No.]

    # parse raw rate to relativity of increment
    ini_rates = [raw_rate[i][i] for i in range(len(raw_rate))]
    mat = []
    for i in range(len(raw_rate)):
        cur = raw_rate[i]
        n_active = 0
        incre = []
        for j in range(len(cur)):
            if cur[j] > ini_rates[j]:
                incre.append(cur[j] - ini_rates[j])
                n_active += 1
            else:
                incre.append(0)

        # total_incre = sum(incre) if sum(incre) else 1
        max_incre = max(incre) if max(incre) else 1
        relativity = [r / max_incre for r in incre]            # normalize with max
        relativity[i] = 1           # self relativity is 1 as a baseline
        mat.append(np.array(relativity))

    # print('relativity:')
    # for row in mat:
    #     for n in row:
    #         if n == 1 or n == 0:
    #             print('%.0f' % n, end=',    ')
    #         else:
    #             print('%.2f' % n, end=', ')
    #     print()
    
    # compute distance among rows to get clusters
    clusters = []
    co_map = {}
    th = 0.3
    for i in range(len(mat)):
        if len(clusters) == 0:
            clusters.append((np.array(mat[i]), 1))
            co_map[i] = 0
            continue
        in_exist_cluster = False
        for j in range(len(clusters)):
            distance = np.linalg.norm(mat[i] - clusters[j][0])
            if distance ** 2 / len(mat[i]) >= th:                       # soft determination here?
                continue
            co_map[i] = j
            w = clusters[j][1]
            clusters[j] = ((clusters[j][0] * w + mat[i]) / (w + 1), w + 1)    # update the mean of cluster
            in_exist_cluster = True
            break
        if not in_exist_cluster:
            clusters.append((mat[i], 1))
            co_map[i] = len(clusters) - 1
    
    return co_map


if __name__ == "__main__":
    raw_rate = [[]] *4
    raw_rate[0] = [          # cornet case: [1][2][3][4]
        [10, 0, 0, 0],
        [0, 10, 0, 0],
        [0, 0, 10, 0],
        [0, 0, 0, 10]
    ]

    raw_rate[1] = [          # normal case: [1,2,3,4]
        [9, 12, 14, 13],
        [12, 10, 15, 12],
        [13, 13, 10, 14],
        [14, 14, 12, 11]
    ]

    raw_rate[2] = [          # normal case: [1,2][3,4]
        [10, 18, 9, 11],
        [19, 9, 10, 11],
        [9, 12, 10, 18],
        [11, 10, 16, 11]
    ]

    raw_rate[3] = [          # normal case: [1,2,3][4]
        [10, 15, 15, 12],
        [13, 9, 18, 10],
        [14, 14, 11, 9],
        [11, 12, 9, 10]
    ]

    for rate in raw_rate:
        print(find_cluster(rate))
        print()