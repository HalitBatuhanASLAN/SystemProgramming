import random, os
random.seed(7777)
L=['ERROR','WARN','INFO','DEBUG']
S=['kernel','nginx','auth','disk','app','db']
W=['error','fail','timeout','retry','connection','process','memory','login','request','response']
with open('huge.log','w') as f:
    for i in range(1500000):
        f.write('[2025-03-10 12:%02d:%02d] [%s] [%s] %s\n' % ((i//3600)%24,(i//60)%60,random.choice(L),random.choice(S),' '.join(random.choices(W,k=8))))
print('huge.log: %.1f MB' % (os.path.getsize('huge.log')/1048576))
