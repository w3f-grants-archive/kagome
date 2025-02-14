# How to run KAGOME validators

## Make chain spec template

```
> substrate build-spec --chain local > testchain.json
```

## Get Sr25519 key-pair for needed accounts

```
> substrate key inspect --scheme Sr25519 //Alice | egrep "seed|key"
  Secret seed:       0xe5be9a5092b81bca64be81d212e7f2f9eba183bb7a90954f7b76361f6edb5c0a
  Public key (hex):  0xd43593c715fdd31c61141abd04a99fd6822c8558854ccde39a5684e7a56da27d
  Public key (SS58): 5GrwvaEF5zXb26Fz9rcQpDWS57CtERHpNehXCPcNoHGKutQY
> substrate key inspect --scheme Sr25519 //Bob | egrep "seed|key"
  Secret seed:       0x398f0c28f98885e046333d4a41c19cee4c37368a9832c6502f6cfd182e2aef89
  Public key (hex):  0x8eaf04151687736326c9fea17e25fc5287613693c912909cb226aa4794f26a48
  Public key (SS58): 5FHneW46xGXgs5mUiveU4sbTyGBzmstUspZC92UhjJM694ty
> substrate key inspect --scheme Sr25519 //Charlie | egrep "seed|key"
  Secret seed:       0xbc1ede780f784bb6991a585e4f6e61522c14e1cae6ad0895fb57b9a205a8f938
  Public key (hex):  0x90b5ab205c6974c9ea841be688864633dc9ca8a357843eeacf2314649965fe22
  Public key (SS58): 5FLSigC9HGRKVhB9FiEo4Y3koPsNmBmLJbpXg2mp1hXcS59Y
> substrate key inspect --scheme Sr25519 //Dave | egrep "seed|key"
  Secret seed:       0x868020ae0687dda7d57565093a69090211449845a7e11453612800b663307246
  Public key (hex):  0x306721211d5404bd9da88e0204360a1a9ab8b87c66c1bc2fcdd37f3c2222cc20
  Public key (SS58): 5DAAnrj7VHTznn2AWBemMuyBwZWs6FNFjdyVXUeYum3PTXFy
```

## use SS58 public keys from previous step as babe authorities

      "babe": {
        "authorities": [
          "5GrwvaEF5zXb26Fz9rcQpDWS57CtERHpNehXCPcNoHGKutQY",
          "5FHneW46xGXgs5mUiveU4sbTyGBzmstUspZC92UhjJM694ty",
          "5FLSigC9HGRKVhB9FiEo4Y3koPsNmBmLJbpXg2mp1hXcS59Y",
          "5DAAnrj7VHTznn2AWBemMuyBwZWs6FNFjdyVXUeYum3PTXFy"
        ],

## Get Ed25519 key-pair for needed accounts

```
> substrate key inspect --scheme Ed25519 //Alice | egrep "seed|key"
  Secret seed:       0xabf8e5bdbe30c65656c0a3cbd181ff8a56294a69dfedd27982aace4a76909115
  Public key (hex):  0x88dc3417d5058ec4b4503e0c12ea1a0a89be200fe98922423d4334014fa6b0ee
  Public key (SS58): 5FA9nQDVg267DEd8m1ZypXLBnvN7SFxYwV7ndqSYGiN9TTpu
> substrate key inspect --scheme Ed25519 //Bob | egrep "seed|key"
  Secret seed:       0x3b7b60af2abcd57ba401ab398f84f4ca54bd6b2140d2503fbcf3286535fe3ff1
  Public key (hex):  0xd17c2d7823ebf260fd138f2d7e27d114c0145d968b5ff5006125f2414fadae69
  Public key (SS58): 5GoNkf6WdbxCFnPdAnYYQyCjAKPJgLNxXwPjwTh6DGg6gN3E
> substrate key inspect --scheme Ed25519 //Charlie | egrep "seed|key"
  Secret seed:       0x072c02fa1409dc37e03a4ed01703d4a9e6bba9c228a49a00366e9630a97cba7c
  Public key (hex):  0x439660b36c6c03afafca027b910b4fecf99801834c62a5e6006f27d978de234f
  Public key (SS58): 5DbKjhNLpqX3zqZdNBc9BGb4fHU1cRBaDhJUskrvkwfraDi6
> substrate key inspect --scheme Ed25519 //Dave | egrep "seed|key"
  Secret seed:       0x771f47d3caf8a2ee40b0719e1c1ecbc01d73ada220cf08df12a00453ab703738
  Public key (hex):  0x5e639b43e0052c47447dac87d6fd2b6ec50bdd4d0f614e4299c665249bbd09d9
  Public key (SS58): 5ECTwv6cZ5nJQPk6tWfaTrEk8YH2L7X1VT4EL5Tx2ikfFwb7
```

## Use SS58 public keys from previous step as grandpa authorities

      "grandpa": {
        "authorities": [
          ["5FA9nQDVg267DEd8m1ZypXLBnvN7SFxYwV7ndqSYGiN9TTpu", 1],
          ["5GoNkf6WdbxCFnPdAnYYQyCjAKPJgLNxXwPjwTh6DGg6gN3E", 1],
          ["5DbKjhNLpqX3zqZdNBc9BGb4fHU1cRBaDhJUskrvkwfraDi6", 1],
          ["5ECTwv6cZ5nJQPk6tWfaTrEk8YH2L7X1VT4EL5Tx2ikfFwb7", 1]
        ]
      },

# Get Ed25519 key-pair related with needed accounts. There is used accounts + //libp2p for determinity. In general, any Ed25519 key-pair can be used.

```
> substrate key inspect --scheme Ed25519 //Alice//libp2p | egrep "seed|hex"
  Secret seed:       0x95d11fd8fc41db64f8f90fecccc1420af9a3ff8dacef6d6a96c3679177860516
  Public key (hex):  0x63e6f7d528e1a626418c31a7112faeb080eb89bb495fc06642d4b0e96684ade8
> substrate key inspect --scheme Ed25519 //Bob//libp2p | egrep "seed|hex"
  Secret seed:       0xa9c7609d2c48c88f5ca3b147fc946ae0664dae7d824a2549379127ef3eadea35
  Public key (hex):  0x5b30c079ebb0a40b24ab8f9b7019ef846e4a1c937c275577bda9da777c36225c
> substrate key inspect --scheme Ed25519 //Charlie//libp2p | egrep "seed|hex"
  Secret seed:       0xf69b297baa6a2a1d2652ee0f72bf07f45fc92c0facc07250679dcc164c53e9a5
  Public key (hex):  0x27971c8d1b663ab17e071bf12274d6edb6dd61983cb159b029b0d49fdf40e01f
> substrate key inspect --scheme Ed25519 //Dave//libp2p | egrep "seed|hex"
  Secret seed:       0xe0d0ba382d0c82b8e1bffc23e316bb9dac07a63408b83480402aee1499515c72
  Public key (hex):  0x2fe8747165fea10fd37ef52837a5e3291d1d9e71aca1135e7679eeac5b69865e
```

## use public keys from previous step to make peer ids (multihash)

```
12D3KooWGYLoNGrZn2nwewBiPFZuKHZebPDL9QAF26cVgLxwuiTZ
12D3KooWFxLPKkB2vLvTFau7wu26YJAXkKmvPHvuVZqmYbV43bMD
12D3KooWCUuiJCHFU1zxNZgnyERYYuHxXzBgoGCaGPAyQvz29s7C
12D3KooWD3Nv1qMBSv6xkJwMxDQkUushJnf9cNw2Ey1uYfCcgKau
```

## use peed ids from previous step in address of bootnodes

```
  "bootNodes": [
    "/ip4/127.0.0.1/tcp/10001/p2p/12D3KooWGYLoNGrZn2nwewBiPFZuKHZebPDL9QAF26cVgLxwuiTZ",
    "/ip4/127.0.0.1/tcp/10002/p2p/12D3KooWFxLPKkB2vLvTFau7wu26YJAXkKmvPHvuVZqmYbV43bMD",
    "/ip4/127.0.0.1/tcp/10003/p2p/12D3KooWCUuiJCHFU1zxNZgnyERYYuHxXzBgoGCaGPAyQvz29s7C",
    "/ip4/127.0.0.1/tcp/10004/p2p/12D3KooWD3Nv1qMBSv6xkJwMxDQkUushJnf9cNw2Ey1uYfCcgKau"
  ],
```

## Start kagome

```
DIR=kagome/examples/network_x4
cd $DIR
kagome --chain testchain4v.json --validator --base-path alice   --name Alice   -p 10001 --rpc-port 11001 --ws-port 12001 --prometheus-port 13001 -lverbose
kagome --chain testchain4v.json --validator --base-path bob     --name Bob     -p 10002 --rpc-port 11002 --ws-port 12002 --prometheus-port 13002 -lverbose
kagome --chain testchain4v.json --validator --base-path charlie --name Charlie -p 10003 --rpc-port 11003 --ws-port 12003 --prometheus-port 13003 -lverbose
kagome --chain testchain4v.json --validator --base-path dave    --name Dave    -p 10004 --rpc-port 11004 --ws-port 12004 --prometheus-port 13004 -lverbose

run --bin substrate -- --chain $DIR/testchain4v.json --base-path=~/.local/share/substrate/alice   --alice   --port=10001 --rpc-port=11001 --ws-port=12001 --prometheus-port=13001 --node-key=95d11fd8fc41db64f8f90fecccc1420af9a3ff8dacef6d6a96c3679177860516
run --bin substrate -- --chain $DIR/testchain4v.json --base-path=~/.local/share/substrate/bob     --bob     --port=10002 --rpc-port=11002 --ws-port=12002 --prometheus-port=13002 --node-key=a9c7609d2c48c88f5ca3b147fc946ae0664dae7d824a2549379127ef3eadea35
run --bin substrate -- --chain $DIR/testchain4v.json --base-path=~/.local/share/substrate/charlie --charlie --port=10003 --rpc-port=11003 --ws-port=12003 --prometheus-port=13003 --node-key=f69b297baa6a2a1d2652ee0f72bf07f45fc92c0facc07250679dcc164c53e9a5
run --bin substrate -- --chain $DIR/testchain4v.json --base-path=~/.local/share/substrate/dave    --dave    --port=10004 --rpc-port=11004 --ws-port=12004 --prometheus-port=13004 --node-key=e0d0ba382d0c82b8e1bffc23e316bb9dac07a63408b83480402aee1499515c72
```