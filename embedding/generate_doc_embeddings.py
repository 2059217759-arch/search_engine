#!/usr/bin/env python3
"""Offline: generate document embeddings from pages.dat and save as binary."""

import struct
import re
import sys
from xml.etree import ElementTree as ET

import numpy as np
from sentence_transformers import SentenceTransformer


MAGIC = 0x44454D42  # "DEMB"
MODEL_NAME = "BAAI/bge-small-zh-v1.5"
PAGES_FILE = "data/pages.dat"
OUTPUT_FILE = "data/doc_embeddings.dat"
BATCH_SIZE = 32


def parse_pages(path: str):
    """Parse pages.dat XML and yield (doc_id, title, content) tuples."""
    with open(path, "r", encoding="utf-8") as f:
        xml_text = f.read()

    # pages.dat is concatenated <doc>...</doc> blocks
    # parse each block individually to avoid malformed-character issues
    for m in re.finditer(r"<doc>.*?</doc>", xml_text, re.DOTALL):
        try:
            doc_el = ET.fromstring(m.group())
        except ET.ParseError:
            continue

        id_el = doc_el.find("id")
        title_el = doc_el.find("title")
        content_el = doc_el.find("content")

        doc_id = int(id_el.text) if id_el is not None and id_el.text else 0
        title = title_el.text or "" if title_el is not None else ""
        content = content_el.text or "" if content_el is not None else ""

        text = title + " " + content
        yield doc_id, text


def main():
    print(f"Loading model: {MODEL_NAME}")
    model = SentenceTransformer(MODEL_NAME)

    print(f"Parsing pages from: {PAGES_FILE}")
    doc_ids = []
    texts = []
    for doc_id, text in parse_pages(PAGES_FILE):
        doc_ids.append(doc_id)
        texts.append(text)

    if not texts:
        print("ERROR: No documents found in pages.dat")
        sys.exit(1)

    print(f"Encoding {len(texts)} documents (dim={model.get_sentence_embedding_dimension()})...")
    embeddings = model.encode(
        texts,
        batch_size=BATCH_SIZE,
        normalize_embeddings=True,
        show_progress_bar=True,
    )  # shape: (N, dim), float32

    dim = embeddings.shape[1]

    print(f"Writing embeddings to: {OUTPUT_FILE}")
    with open(OUTPUT_FILE, "wb") as f:
        f.write(struct.pack("<I", MAGIC))
        f.write(struct.pack("<i", len(doc_ids)))
        f.write(struct.pack("<i", dim))
        for doc_id, emb in zip(doc_ids, embeddings):
            f.write(struct.pack("<i", doc_id))
            f.write(emb.astype(np.float32).tobytes())

    size_mb = len(doc_ids) * (4 + dim * 4) / (1024 * 1024)
    print(f"Done! {len(doc_ids)} docs, dim={dim}, ~{size_mb:.1f} MB")


if __name__ == "__main__":
    main()
