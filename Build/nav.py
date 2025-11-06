import json

final_data = []
for i in range(1,50):
    data = {
            "actorID": str(i),
            "src": [
                -6776.816428303229,
                -12590.548065851965,
                0
            ],
            "dist": [
                -10710.589801851864,
                950.3394480285897,
                0
            ]
        }
    final_data.append(data)


with open("out_data.txt", "w") as f:
    
    f.write(json.dumps(final_data))