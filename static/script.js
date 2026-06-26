(function () {
  const input = document.getElementById('searchInput');
  const btn = document.getElementById('searchBtn');
  const suggestionsEl = document.getElementById('suggestions');
  const resultsArea = document.getElementById('resultsArea');
  const header = document.querySelector('.header');
  const footerInfo = document.getElementById('footerInfo');
  const statusMsg = document.getElementById('statusMsg');
  const hotList = document.getElementById('hotList');

  let suggestIdx = -1;
  let suggestData = [];
  let currentQuery = '';

  // ── Health check ──
  fetch('/health')
    .then(r => r.json())
    .then(d => {
      if (d && d.docs != null) {
        footerInfo.textContent = '索引文档数: ' + d.docs.toLocaleString();
      }
    })
    .catch(() => {});

  // ── Hot pages ──
  function loadHotPages() {
    fetch('/hot?k=10')
      .then(r => r.json())
      .then(data => {
        renderHotPages(data);
      })
      .catch(() => {
        hotList.innerHTML = '<li class="hot-loading">加载失败</li>';
      });
  }

  function renderHotPages(data) {
    if (!Array.isArray(data) || data.length === 0) {
      hotList.innerHTML = '<li class="hot-loading">暂无数据<br><span style="font-size:0.75rem;">搜索后出现</span></li>';
      return;
    }

    hotList.innerHTML = data.map(item => {
      const title = escapeHtml(item.title || '无标题');
      const link = escapeHtml(item.link || '');
      const rank = item.rank || 0;
      const clickCount = item.clickCount || 0;
      const rankClass = rank <= 3 ? ' top' + rank : '';
      return (
        '<li class="hot-item' + rankClass + '" data-id="' + item.id + '" data-link="' + escapeAttr(item.link) + '" title="' + escapeAttr(item.title) + '">' +
          '<span class="hot-rank">' + rank + '</span>' +
          '<div class="hot-info">' +
            '<div class="hot-item-title">' + title + '<span class="hot-click-count">' + clickCount + '</span></div>' +
          '</div>' +
        '</li>'
      );
    }).join('');

    // 点击跳转 + 上报点击数
    hotList.querySelectorAll('.hot-item').forEach(el => {
      el.addEventListener('click', function () {
        const docId = this.getAttribute('data-id');
        const url = this.getAttribute('data-link');
        // 上报点击
        if (docId) {
          fetch('/click?id=' + encodeURIComponent(docId), { method: 'POST' })
            .catch(() => {});
        }
        if (url) window.open(url, '_blank');
      });
    });
  }

  // 首次加载
  loadHotPages();

  // ── Debounced suggest ──
  let suggestTimer = null;
  function requestSuggest(q) {
    clearTimeout(suggestTimer);
    if (!q.trim()) {
      hideSuggestions();
      return;
    }
    suggestTimer = setTimeout(() => {
      fetch('/suggest?q=' + encodeURIComponent(q))
        .then(r => r.json())
        .then(data => {
          if (!Array.isArray(data) || data.length === 0) {
            hideSuggestions();
            return;
          }
          suggestData = data;
          suggestIdx = -1;
          renderSuggestions();
        })
        .catch(() => hideSuggestions());
    }, 300);
  }

  function renderSuggestions() {
    if (suggestData.length === 0) {
      hideSuggestions();
      return;
    }
    suggestionsEl.innerHTML = suggestData
      .map((item, i) => {
        const icon = item.type === 'correction' ? '&#8635;' : '&#128269;';
        const tag = item.type === 'correction' ? '纠错' : '';
        return (
          '<li class="suggestion-item' + (i === suggestIdx ? ' active' : '') + '" data-idx="' + i + '">' +
            '<span class="suggestion-icon">' + icon + '</span>' +
            '<span class="suggestion-word">' + escapeHtml(item.word) + '</span>' +
            (tag ? '<span class="suggestion-type">' + tag + '</span>' : '') +
          '</li>'
        );
      })
      .join('');
    suggestionsEl.classList.remove('hidden');
  }

  function hideSuggestions() {
    suggestionsEl.classList.add('hidden');
    suggestData = [];
    suggestIdx = -1;
  }

  // ── Search ──
  function doSearch(q) {
    q = q.trim();
    if (!q) return;
    currentQuery = q;
    input.value = q;
    hideSuggestions();
    header.classList.add('compact');
    window.scrollTo({ top: 0, behavior: 'smooth' });

    resultsArea.innerHTML =
      '<div class="search-info">搜索: <strong>' + escapeHtml(q) + '</strong></div>' +
      '<div class="loading"><span class="spinner"></span> 搜索中...</div>';

    fetch('/search?q=' + encodeURIComponent(q))
      .then(r => r.json())
      .then(data => {
        renderResults(q, data);
        // 搜索完成后刷新热门面板
        loadHotPages();
      })
      .catch(() => {
        resultsArea.innerHTML =
          '<div class="search-info">搜索: <strong>' + escapeHtml(q) + '</strong></div>' +
          '<div class="error-msg">搜索请求失败，请检查后端服务是否正常运行</div>';
      });
  }

  function renderResults(query, data) {
    if (!Array.isArray(data) || data.length === 0) {
      resultsArea.innerHTML =
        '<div class="search-info">搜索: <strong>' + escapeHtml(query) + '</strong></div>' +
        '<div class="status-msg">' +
          '<p style="font-size:2rem;margin-bottom:8px;">&#128533;</p>' +
          '<p>未找到与 <strong>' + escapeHtml(query) + '</strong> 相关的结果</p>' +
          '<p style="margin-top:6px;font-size:0.85rem;">请尝试其他关键词</p>' +
        '</div>';
      return;
    }

    const keywords = extractKeywords(query);
    let html = '<div class="search-info">找到约 <strong>' + data.length + '</strong> 条结果</div>';

    data.forEach(item => {
      const title = highlightText(item.title || '无标题', keywords);
      const link = escapeHtml(item.link || '');
      const abstract = highlightText(item.abstract || '', keywords);
      const sparseScore = item.sparseScore != null ? item.sparseScore.toFixed(4) : '-';
      const denseScore = item.denseScore != null ? item.denseScore.toFixed(4) : '-';
      html +=
        '<div class="result-item" data-id="' + item.id + '" data-link="' + escapeAttr(item.link) + '">' +
          '<div class="result-header">' +
            '<div class="result-title"><a href="' + escapeAttr(item.link) + '" target="_blank" rel="noopener">' + title + '</a></div>' +
            '<div class="result-scores">' +
              '<span class="score-badge score-sparse" title="稀疏检索得分 (TF-IDF)">稀疏 ' + sparseScore + '</span>' +
              '<span class="score-badge score-dense" title="稠密检索得分 (Dense)">稠密 ' + denseScore + '</span>' +
            '</div>' +
          '</div>' +
          '<div class="result-link">' + (link ? link : '&nbsp;') + '</div>' +
          '<div class="result-abstract">' + abstract + '</div>' +
        '</div>';
    });
    resultsArea.innerHTML = html;

    // 搜索结果点击上报
    resultsArea.querySelectorAll('.result-item').forEach(el => {
      el.addEventListener('click', function (e) {
        // 不阻止链接默认行为（新标签页打开），只上报
        const docId = this.getAttribute('data-id');
        if (docId) {
          fetch('/click?id=' + encodeURIComponent(docId), { method: 'POST' })
            .catch(() => {});
        }
      });
    });
  }

  function extractKeywords(query) {
    // split Chinese + English words
    const tokens = [];
    // English words
    const en = query.match(/[a-zA-Z0-9]+/g) || [];
    tokens.push(...en);
    // Chinese characters as individual tokens
    const cn = query.match(/[一-鿿]+/g) || [];
    cn.forEach(s => {
      for (let i = 0; i < s.length; i++) tokens.push(s[i]);
    });
    return [...new Set(tokens.map(t => t.toLowerCase()))];
  }

  function highlightText(text, keywords) {
    if (!keywords.length) return escapeHtml(text);
    const escaped = escapeHtml(text);
    const pattern = keywords
      .map(k => k.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'))
      .join('|');
    if (!pattern) return escaped;
    const re = new RegExp('(' + pattern + ')', 'gi');
    return escaped.replace(re, '<em>$1</em>');
  }

  // ── Event listeners ──
  input.addEventListener('input', function () {
    requestSuggest(this.value);
  });

  input.addEventListener('keydown', function (e) {
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (suggestData.length === 0) return;
      suggestIdx = (suggestIdx + 1) % suggestData.length;
      renderSuggestions();
      scrollToActive();
      return;
    }
    if (e.key === 'ArrowUp') {
      e.preventDefault();
      if (suggestData.length === 0) return;
      suggestIdx = (suggestIdx - 1 + suggestData.length) % suggestData.length;
      renderSuggestions();
      scrollToActive();
      return;
    }
    if (e.key === 'Enter') {
      e.preventDefault();
      if (suggestIdx >= 0 && suggestIdx < suggestData.length) {
        doSearch(suggestData[suggestIdx].word);
      } else {
        doSearch(this.value);
      }
      return;
    }
    if (e.key === 'Escape') {
      hideSuggestions();
    }
  });

  btn.addEventListener('click', function () {
    doSearch(input.value);
  });

  suggestionsEl.addEventListener('click', function (e) {
    const item = e.target.closest('.suggestion-item');
    if (!item) return;
    const idx = parseInt(item.getAttribute('data-idx'), 10);
    if (idx >= 0 && idx < suggestData.length) {
      doSearch(suggestData[idx].word);
    }
  });

  document.addEventListener('click', function (e) {
    if (!e.target.closest('.search-box-wrapper')) {
      hideSuggestions();
    }
  });

  function scrollToActive() {
    const active = suggestionsEl.querySelector('.suggestion-item.active');
    if (active) {
      active.scrollIntoView({ block: 'nearest' });
    }
  }

  function escapeHtml(str) {
    const div = document.createElement('div');
    div.appendChild(document.createTextNode(str));
    return div.innerHTML;
  }

  function escapeAttr(str) {
    return str.replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }

  // Focus input on '/' key
  document.addEventListener('keydown', function (e) {
    if (e.key === '/' && document.activeElement !== input && !e.target.closest('input,textarea,[contenteditable]')) {
      e.preventDefault();
      input.focus();
    }
  });
})();
