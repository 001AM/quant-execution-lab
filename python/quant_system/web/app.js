const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];

const palette = ["#15aa79", "#3979e8", "#f3a82f"];
const pageCopy = {
  market: ["Step 1 of 4", "Connect market data"],
  backtest: ["Step 2 of 4", "Compare strategy performance"],
  research: ["Step 3 of 4", "Validate out of sample"],
  execution: ["Step 4 of 4", "Simulate order execution"],
};
const state = { source: "yahoo", csvText: "", market: null };
const price = new Intl.NumberFormat("en-US", { minimumFractionDigits: 2, maximumFractionDigits: 2 });
const compact = new Intl.NumberFormat("en-US", { notation: "compact", maximumFractionDigits: 1 });
const ratio = new Intl.NumberFormat("en-US", { maximumFractionDigits: 2 });

function escapeHtml(value) {
  return String(value).replace(/[&<>'"]/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;" })[char]);
}

function titleCase(value) {
  return String(value).replaceAll("_", " ").replace(/\b\w/g, (letter) => letter.toUpperCase());
}

function percent(value) {
  return value == null ? "—" : `${value >= 0 ? "+" : ""}${(value * 100).toFixed(2)}%`;
}

function formatCurrency(value, currency = state.market?.currency || "USD", maximumFractionDigits = 0) {
  return new Intl.NumberFormat(currency === "INR" ? "en-IN" : "en-US", {
    style: "currency",
    currency,
    maximumFractionDigits,
  }).format(value);
}

function toast(message, error = false) {
  const element = $("#toast");
  element.textContent = message;
  element.className = `toast show${error ? " error" : ""}`;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => element.className = "toast", 3800);
}

async function post(path, payload = {}) {
  const response = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  const data = await response.json();
  if (!response.ok) {
    const validation = Array.isArray(data.detail) ? data.detail.map((item) => item.msg).join("; ") : data.detail;
    throw new Error(data.error || validation || "The request could not be completed.");
  }
  return data;
}

function dataPayload() {
  return {
    source: state.source,
    symbol: $("#symbol").value.trim().toUpperCase(),
    exchange: $("#exchange").value,
    period: $("#period").value,
    data_path: $("#data-path").value.trim(),
    csv_text: state.csvText,
  };
}

function backtestPayload() {
  return {
    ...dataPayload(),
    strategy: $("#strategy").value,
    initial_capital: Number($("#capital").value),
    quantity: Number($("#quantity").value),
    commission: Number($("#commission").value),
    slippage: Number($("#slippage").value),
    allow_short: $("#allow-short").checked,
  };
}

function navigate(viewName) {
  $$(".nav-link").forEach((button) => {
    const active = button.dataset.view === viewName;
    button.classList.toggle("active", active);
    button.setAttribute("aria-current", active ? "page" : "false");
  });
  $$(".view").forEach((view) => {
    const active = view.id === `view-${viewName}`;
    view.hidden = !active;
    view.classList.toggle("active", active);
  });
  $("#page-overline").textContent = pageCopy[viewName][0];
  $("#page-title").textContent = pageCopy[viewName][1];
  window.scrollTo({ top: 0, behavior: "smooth" });
}

function setBusy(button, busy, labelText) {
  if (!button.dataset.label) button.dataset.label = button.querySelector("span").textContent;
  button.disabled = busy;
  button.querySelector("span").textContent = busy ? labelText : button.dataset.label;
}

function downsample(points, maximum = 180) {
  if (points.length <= maximum) return points;
  const step = (points.length - 1) / (maximum - 1);
  return Array.from({ length: maximum }, (_, index) => points[Math.round(index * step)]);
}

function renderLineChart(target, series, options = {}) {
  const host = $(target);
  const usable = series.filter((item) => item.points?.length).map((item) => ({ ...item, points: downsample(item.points) }));
  if (!usable.length) {
    host.innerHTML = '<p class="empty-message">No chart data returned.</p>';
    return;
  }
  const width = 760, height = options.height || 260;
  const left = options.axis === false ? 8 : 57, right = 9, top = 12, bottom = options.axis === false ? 8 : 27;
  const values = usable.flatMap((item) => item.points.map((point) => point.value));
  const min = Math.min(...values), max = Math.max(...values);
  const spread = max - min || Math.max(Math.abs(max) * .02, 1);
  const low = min - spread * .1, high = max + spread * .1;
  const longest = Math.max(...usable.map((item) => item.points.length));
  const x = (index) => left + (index / Math.max(longest - 1, 1)) * (width - left - right);
  const y = (value) => top + ((high - value) / (high - low)) * (height - top - bottom);
  const grid = options.axis === false ? "" : [0, 1, 2, 3].map((index) => {
    const yy = top + index * ((height - top - bottom) / 3);
    const value = high - index * ((high - low) / 3);
    return `<line class="chart-grid" x1="${left}" y1="${yy}" x2="${width - right}" y2="${yy}"/><text class="chart-label" x="0" y="${yy + 4}">${options.currency === false ? price.format(value) : formatCurrency(value, options.currencyCode)}</text>`;
  }).join("");
  const paths = usable.map((item, seriesIndex) => {
    const points = item.points.map((point, index) => `${x(index)},${y(point.value)}`).join(" ");
    const base = height - bottom;
    return `<polygon class="chart-area" fill="${palette[seriesIndex]}" points="${left},${base} ${points} ${x(item.points.length - 1)},${base}"/><polyline class="chart-line" stroke="${palette[seriesIndex]}" points="${points}"/>`;
  }).join("");
  const xLabels = options.axis === false ? "" : `<text class="chart-label" x="${left}" y="${height - 4}">${escapeHtml(usable[0].points[0].label || "First bar")}</text><text class="chart-label" text-anchor="end" x="${width - right}" y="${height - 4}">${escapeHtml(usable[0].points.at(-1).label || "Latest bar")}</text>`;
  host.innerHTML = `<svg viewBox="0 0 ${width} ${height}" role="img" aria-label="${escapeHtml(options.label || "Value over time")}">${grid}${paths}${xLabels}</svg>`;
}

function renderMarketChart(prices) {
  const points = downsample(prices, 150);
  const width = 760, height = 230, left = 3, right = 3, top = 10, bottom = 32;
  const closes = points.map((point) => point.close), volumes = points.map((point) => point.volume);
  const min = Math.min(...closes), max = Math.max(...closes), spread = max - min || 1, volumeMax = Math.max(...volumes) || 1;
  const x = (index) => left + (index / Math.max(points.length - 1, 1)) * (width - left - right);
  const y = (value) => top + ((max + spread * .08 - value) / (spread * 1.16)) * (height - top - bottom - 30);
  const line = points.map((point, index) => `${x(index)},${y(point.close)}`).join(" ");
  const baseline = height - bottom - 30;
  const bars = points.map((point, index) => {
    const barHeight = (point.volume / volumeMax) * 25;
    return `<rect class="volume-bar" x="${x(index)}" y="${height - bottom - barHeight}" width="${Math.max((width / points.length) - 2, 1)}" height="${barHeight}"/>`;
  }).join("");
  const first = new Date(points[0].timestamp).toLocaleDateString(undefined, { month: "short", year: "numeric" });
  const last = new Date(points.at(-1).timestamp).toLocaleDateString(undefined, { month: "short", year: "numeric" });
  $("#market-chart").innerHTML = `<svg viewBox="0 0 ${width} ${height}" role="img" aria-label="Closing price and volume history">${bars}<polygon class="chart-area" fill="#15aa79" points="${left},${baseline} ${line} ${width - right},${baseline}"/><polyline class="chart-line" stroke="#15aa79" points="${line}"/><text class="chart-label" x="${left}" y="${height - 5}">${first}</text><text class="chart-label" text-anchor="end" x="${width - right}" y="${height - 5}">${last}</text></svg>`;
}

function setSource(source) {
  state.source = source;
  $$(".source-option").forEach((button) => {
    const active = button.dataset.source === source;
    button.classList.toggle("active", active);
    button.setAttribute("aria-checked", String(active));
  });
  $("#yahoo-fields").classList.toggle("hidden", source !== "yahoo");
  $("#csv-fields").classList.toggle("hidden", source !== "csv");
  $("#source-note").innerHTML = source === "yahoo"
    ? '<span class="secure-icon">✓</span> No API key required. Data is cached briefly in memory.'
    : '<span class="secure-icon">✓</span> Files stay on this machine and use the same validation rules.';
}

function renderMarket(data) {
  const previousCurrency = state.market?.currency;
  state.market = data;
  const changeClass = data.price_change >= 0 ? "positive" : "negative";
  $("#ticker-avatar").textContent = data.symbol[0];
  $("#market-symbol").textContent = data.symbol;
  $("#market-source").textContent = data.provider === "yahoo" ? `Yahoo Finance · ${data.exchange} · Daily` : "CSV dataset · Daily";
  $("#market-freshness").textContent = `Through ${new Date(data.end).toLocaleDateString(undefined, { month: "short", day: "numeric", year: "numeric" })}`;
  $("#last-close").textContent = formatCurrency(data.last_close, data.currency, 2);
  $("#price-change").textContent = `${percent(data.price_change)} this period`;
  $("#price-change").className = `change-badge ${changeClass}`;
  $("#market-stats").innerHTML = `<div><span>Period high</span><strong>${formatCurrency(data.period_high, data.currency, 2)}</strong></div><div><span>Period low</span><strong>${formatCurrency(data.period_low, data.currency, 2)}</strong></div><div><span>Average volume</span><strong>${compact.format(data.average_volume)}</strong></div><div><span>Daily bars</span><strong>${data.bars}</strong></div>`;
  renderMarketChart(data.prices);

  $("#chip-symbol").textContent = data.symbol;
  $("#chip-detail").textContent = `${data.exchange} · ${data.bars} daily bars · ${data.currency}`;
  $(".provider-mark").textContent = data.provider === "yahoo" ? "YF" : "CSV";
  $(".provider-mark").style.background = data.provider === "yahoo" ? "#6d36a4" : "#3979e8";
  $("#backtest-context").textContent = `${data.symbol} · ${data.exchange} · ${data.bars} bars · ${data.currency}`;
  $("#research-context").textContent = `${data.symbol} · ${data.exchange} · ${data.bars} bars · ${data.currency}`;
  $("#execution-context").textContent = `${data.symbol} · ${data.exchange} · ${data.bars} validated bars`;
  $("#execution-context-detail").textContent = `Latest close ${formatCurrency(data.last_close, data.currency, 2)} · ${new Date(data.end).toLocaleDateString()} · Yahoo data may be delayed`;
  $("#execution-market-copy").textContent = `Schedules ${data.symbol} at the latest validated close and reports fill rate, VWAP, arrival cost, and fees.`;
  $("#execution-reference").textContent = `${data.symbol} · ${formatCurrency(data.last_close, data.currency, 2)}`;
  $("#capital-prefix").textContent = data.currency === "INR" ? "₹" : "$";
  if (previousCurrency !== data.currency) {
    $("#capital").value = data.currency === "INR" ? 1000000 : 100000;
    $("#portfolio-capital").value = data.currency === "INR" ? 1000000 : 100000;
  }

  const train = Math.max(8, Math.min(60, Math.floor(data.bars * .55)));
  const test = Math.max(3, Math.min(20, Math.floor((data.bars - train) / 2)));
  $("#train-size").value = train;
  $("#test-size").value = test;
}

async function loadMarket({ quiet = false } = {}) {
  const button = $("#load-market");
  setBusy(button, true, state.source === "yahoo" ? "Connecting to Yahoo Finance…" : "Reading CSV…");
  try {
    const data = await post("/api/dataset", dataPayload());
    renderMarket(data);
    if (!quiet) toast(`${data.symbol}: loaded ${data.bars} validated daily bars.`);
  } catch (error) {
    $("#market-chart").innerHTML = `<p class="empty-message">${escapeHtml(error.message)}</p>`;
    toast(`${error.message}${state.source === "yahoo" ? " Try CSV if the network is unavailable." : ""}`, true);
  } finally {
    setBusy(button, false, "");
  }
}

function resultStat(name, value, className = "") {
  return `<div class="result-stat"><span>${name}</span><strong class="${className}">${value}</strong></div>`;
}

function renderBacktest(data) {
  const winner = [...data.runs].sort((a, b) => b.total_return - a.total_return)[0];
  const maxMagnitude = Math.max(...data.runs.map((run) => Math.abs(run.total_return)), .001);
  $("#backtest-results").innerHTML = `
    <article class="card result-hero">
      <div class="winner-row">
        <div class="winner-name"><span class="winner-icon">★</span><div><p>Best result in this run</p><h3>${escapeHtml(titleCase(winner.strategy))}</h3></div></div>
        <div class="winner-return"><span>Total return</span><strong class="${winner.total_return >= 0 ? "positive" : "negative"}">${percent(winner.total_return)}</strong></div>
      </div>
      <div class="result-stats">
        ${resultStat("Ending equity", formatCurrency(winner.ending_equity, data.currency))}
        ${resultStat("Benchmark", percent(winner.benchmark_return), winner.benchmark_return >= 0 ? "positive" : "negative")}
        ${resultStat("Sharpe ratio", winner.sharpe_ratio == null ? "—" : ratio.format(winner.sharpe_ratio))}
        ${resultStat("Max drawdown", percent(winner.max_drawdown), "negative")}
      </div>
      <div class="panel-head"><div><p class="overline">Account value</p><h3>Equity curve</h3></div><div class="legend">${data.runs.map((run, index) => `<span><i style="background:${palette[index]}"></i>${escapeHtml(titleCase(run.strategy))}</span>`).join("")}</div></div>
      <div class="data-chart" id="backtest-chart"></div>
    </article>
    <article class="card comparison-card">
      <div class="comparison-head"><div><p class="overline">Side by side</p><h3>Return comparison</h3></div><p>Includes commission and slippage</p></div>
      <div class="strategy-bars">${data.runs.map((run) => `<div class="strategy-bar-row"><span>${escapeHtml(titleCase(run.strategy))}</span><div class="bar-track"><i class="${run.total_return >= 0 ? "positive" : "negative"}" style="width:${Math.max(Math.abs(run.total_return) / maxMagnitude * 48, 1)}%"></i></div><b class="${run.total_return >= 0 ? "positive" : "negative"}">${percent(run.total_return)}</b></div>`).join("")}</div>
      <table><thead><tr><th>Strategy</th><th>Fills</th><th>Volatility</th><th>Rejected</th></tr></thead><tbody>${data.runs.map((run) => `<tr><td>${escapeHtml(titleCase(run.strategy))}</td><td>${run.fills}</td><td>${percent(run.volatility)}</td><td>${run.rejected_orders}</td></tr>`).join("")}</tbody></table>
    </article>`;
  renderLineChart("#backtest-chart", data.runs.map((run) => ({ name: run.strategy, points: run.equity.map((point) => ({ value: point.value, label: new Date(point.timestamp).toLocaleDateString(undefined, { month: "short", year: "2-digit" }) })) })), { label: "Strategy equity curves", currencyCode: data.currency });
}

async function runBacktest() {
  const button = $("#run-backtest");
  setBusy(button, true, "Running event-driven simulation…");
  try {
    const data = await post("/api/backtest", backtestPayload());
    renderBacktest(data);
    toast(`Backtest complete for ${data.symbol}.`);
  } catch (error) {
    toast(error.message, true);
  } finally {
    setBusy(button, false, "");
  }
}

function renderResearch(data) {
  $("#research-results").innerHTML = `
    <div class="metric-ribbon">
      ${resultStat("OOS return", percent(data.performance.total_return), data.performance.total_return >= 0 ? "positive" : "negative")}
      ${resultStat("Ending equity", formatCurrency(data.performance.ending_equity, data.currency))}
      ${resultStat("Annual volatility", percent(data.performance.annualized_volatility))}
      ${resultStat("Max drawdown", percent(data.performance.max_drawdown), "negative")}
    </div>
    <div class="research-result-grid">
      <article class="card research-chart-card">
        <div class="panel-head"><div><p class="overline">Unseen windows only</p><h3>Out-of-sample equity</h3></div><small>${data.fills} fills</small></div>
        <div class="data-chart" id="research-chart"></div>
      </article>
      <article class="card fold-card">
        <div class="panel-head"><div><p class="overline">Parameter audit</p><h3>Fold by fold</h3></div><small>${data.folds.length} tests</small></div>
        <div class="fold-list">${data.folds.map((fold) => `<div class="fold-row"><span>${fold.fold}</span><div><b>Lookback ${fold.parameters.lookback} · Threshold ${(fold.parameters.threshold * 100).toFixed(1)}%</b><small>${escapeHtml(fold.test)}</small></div><strong class="${fold.test_return >= 0 ? "positive" : "negative"}">${percent(fold.test_return)}</strong></div>`).join("")}</div>
      </article>
    </div>`;
  renderLineChart("#research-chart", [{ name: "OOS equity", points: data.equity.map((point) => ({ value: point.value, label: new Date(point.timestamp).toLocaleDateString(undefined, { month: "short", year: "2-digit" }) })) }], { label: "Out-of-sample equity", currencyCode: data.currency });
}

async function runWalkForward() {
  const button = $("#run-walk-forward");
  setBusy(button, true, "Validating…");
  try {
    const data = await post("/api/walk-forward", {
      ...backtestPayload(),
      train_size: Number($("#train-size").value),
      test_size: Number($("#test-size").value),
    });
    renderResearch(data);
    toast(`Walk-forward validation completed across ${data.folds.length} folds.`);
  } catch (error) {
    toast(error.message, true);
  } finally {
    setBusy(button, false, "");
  }
}

async function runExecution() {
  const button = $("#run-execution");
  setBusy(button, true, "Running native engine…");
  $("#output-title").textContent = "Running execution algorithms";
  $(".output-status").textContent = "Working";
  try {
    const data = await post("/api/execution", {
      ...dataPayload(),
      execution_quantity: Number($("#execution-quantity").value),
    });
    $("#output-title").textContent = `${data.symbol} · TWAP · VWAP · POV`;
    $(".output-status").textContent = "Real market input";
    $("#execution-output").innerHTML = `<pre>${escapeHtml(data.output)}</pre>`;
    toast(`${data.symbol}: native execution simulation complete.`);
  } catch (error) {
    $("#output-title").textContent = "Execution could not start";
    $(".output-status").textContent = "Action needed";
    $("#execution-output").innerHTML = `<p>${escapeHtml(error.message)}</p>`;
    toast(error.message, true);
  } finally {
    setBusy(button, false, "");
  }
}

async function runPortfolio() {
  const button = $("#run-portfolio");
  setBusy(button, true, "Valuing selected position…");
  try {
    const data = await post("/api/portfolio", {
      ...dataPayload(),
      portfolio_capital: Number($("#portfolio-capital").value),
      portfolio_quantity: Number($("#portfolio-quantity").value),
      commission: Number($("#commission").value),
    });
    const stats = [["Entry price", data.entry_price], ["Latest close", data.market_price], ["Cash", data.cash], ["Equity", data.equity], ["Unrealized P&L", data.unrealized_pnl], ["Gross exposure", data.gross_exposure]];
    $("#output-title").textContent = `${data.symbol} · portfolio snapshot`;
    $(".output-status").textContent = "Real market input";
    $("#execution-output").innerHTML = `<div class="portfolio-grid">${stats.map(([name, value]) => `<div class="portfolio-stat"><span>${name}</span><strong class="${name.includes("P&L") ? (value >= 0 ? "positive" : "negative") : ""}">${formatCurrency(value, data.currency, 2)}</strong></div>`).join("")}<div class="positions"><table><thead><tr><th>Symbol</th><th>Quantity</th><th>Average price</th></tr></thead><tbody>${data.positions.map((position) => `<tr><td>${escapeHtml(position.symbol)}</td><td>${position.quantity}</td><td>${formatCurrency(position.average_price, data.currency, 2)}</td></tr>`).join("")}</tbody></table></div></div>`;
    toast(`${data.symbol}: position valued through the latest loaded close.`);
  } catch (error) {
    toast(error.message, true);
  } finally {
    setBusy(button, false, "");
  }
}

$$('[data-go]').forEach((element) => element.addEventListener("click", (event) => {
  event.preventDefault();
  navigate(element.dataset.go);
}));
$$(".nav-link").forEach((button) => button.addEventListener("click", () => navigate(button.dataset.view)));
$$(".source-option").forEach((button) => button.addEventListener("click", () => setSource(button.dataset.source)));
$$("[data-symbol]").forEach((button) => button.addEventListener("click", () => {
  setSource("yahoo");
  $("#symbol").value = button.dataset.symbol;
  $("#exchange").value = button.dataset.exchange;
  loadMarket();
}));
$("#csv-file").addEventListener("change", async (event) => {
  const file = event.target.files[0];
  state.csvText = file ? await file.text() : "";
  if (file) toast(`${file.name} is ready to load.`);
});
$("#symbol").addEventListener("keydown", (event) => { if (event.key === "Enter") loadMarket(); });
$("#load-market").addEventListener("click", () => loadMarket());
$("#run-backtest").addEventListener("click", runBacktest);
$("#run-walk-forward").addEventListener("click", runWalkForward);
$("#run-execution").addEventListener("click", runExecution);
$("#run-portfolio").addEventListener("click", runPortfolio);

loadMarket({ quiet: true });
