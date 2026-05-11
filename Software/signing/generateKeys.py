import nacl.signing
import nacl.encoding
import sys

# ---------- Key generation ----------
def generate_keys():
    signing_key = nacl.signing.SigningKey.generate()
    verify_key = signing_key.verify_key

    private_key = signing_key.encode(encoder=nacl.encoding.HexEncoder).decode()
    public_key = verify_key.encode(encoder=nacl.encoding.HexEncoder).decode()

    return private_key, public_key


# ---------- CLI ----------
if __name__ == "__main__":
    priv, pub = generate_keys()
    print("Private Key:", priv)
    print("Public Key :", pub)

