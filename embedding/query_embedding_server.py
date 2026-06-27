#!/usr/bin/env python3
"""FastAPI microservice for query embedding."""

import os

# 离线模式：仅使用本地缓存，不尝试连接 huggingface.co
os.environ["HF_HUB_OFFLINE"] = "1"

from fastapi import FastAPI
from pydantic import BaseModel
from sentence_transformers import SentenceTransformer
import uvicorn


MODEL_NAME = "BAAI/bge-small-zh-v1.5"

app = FastAPI(title="Query Embedding Service")
model = SentenceTransformer(MODEL_NAME)


class EmbedRequest(BaseModel):
    query: str


class EmbedResponse(BaseModel):
    embedding: list[float]
    dim: int


@app.post("/embed")
def embed(req: EmbedRequest):
    vec = model.encode(req.query, normalize_embeddings=True)
    return EmbedResponse(embedding=vec.tolist(), dim=int(len(vec)))


@app.get("/health")
def health():
    return {"status": "ok"}


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8765)
