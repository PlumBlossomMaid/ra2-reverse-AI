import dump_tech_buildings_golden as m
for s in range(1,20):
    o=m.TechOracle(4,4,seed=s)
    o.set_regions([1,2])
    r=o.run(2)
    print(s, len(o.placernd)//2)
