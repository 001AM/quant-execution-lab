const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];

const palette = ["#0eaa72", "#356ae6", "#d98a16"];
const state = { source: "yahoo", csvText: "", market: null, sectorPeriod: "1mo" };
const compact = new Intl.NumberFormat("en-IN", { notation: "compact", maximumFractionDigits: 1 });
const ratio = new Intl.NumberFormat("en-IN", { maximumFractionDigits: 2 });

function escapeHtml(value) {
  return String(value).replace(/[&<>'"]/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;" })[char]);
}

function titleCase(value) {
  return String(value).replaceAll("_", " ").replace(/\b\w/g, (letter) => letter.toUpperCase());
}

function percent(value, digits = 2) {
  return value == null ? "—" : `${value >= 0 ? "+" : ""}${(value * 100).toFixed(digits)}%`;
}

function formatCurrency(value, currency = state.market?.currency || "INR", maximumFractionDigits = 0) {
  return new Intl.NumberFormat(currency === "INR" ? "en-IN" : "en-US", {
    style: "currency",
    currency,
    maximumFractionDigits,
  }).format(value);
}

function shortDate(value) {
  return new Date(value).toLocaleDateString("en-IN", { day: "numeric", month: "short", year: "numeric" });
}

function toast(message, error = false) {
  const element = $("#toast");
  element.textContent = message;
  element.className = `toast show${error ? " error" : ""}`;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => { element.className = "toast"; }, 3800);
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

function setBusy(button, busy, labelText) {
  const label = button.querySelector("span");
  if (!button.dataset.label) button.dataset.label = label.textContent;
  button.disabled = busy;
  label.textContent = busy ? labelText : button.dataset.label;
}

function dataPayload() {
  return {
    source: state.source,
    symbol: $("#symbol").value.trim().toUpperCase(),
    exchange: $("#exchange").value,
    period: $("#period").value,
    data_path: $("#data-path").value.trim(),
    csv_text: state.csvText || null,
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

function riskPayload() {
  return {
    ...dataPayload(),
    risk_capital: Number($("#rms-capital").value),
    risk_quantity: Number($("#rms-quantity").value),
    commission: Number($("#commission").value),
    max_position_pct: Number($("#rms-max-position").value) / 100,
    max_leverage: Number($("#rms-max-leverage").value),
    max_drawdown_pct: Number($("#rms-max-drawdown").value) / 100,
    max_var_pct: Number($("#rms-max-var").value) / 100,
  };
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
  const width = 760;
  const height = options.height || 255;
  const left = 60, right = 10, top = 12, bottom = 27;
  const values = usable.flatMap((item) => item.points.map((point) => point.value));
  const min = Math.min(...values), max = Math.max(...values);
  const spread = max - min || Math.max(Math.abs(max) * .02, 1);
  const low = min - spread * .1, high = max + spread * .1;
  const longest = Math.max(...usable.map((item) => item.points.length));
  const x = (index) => left + (index / Math.max(longest - 1, 1)) * (width - left - right);
  const y = (value) => top + ((high - value) / (high - low)) * (height - top - bottom);
  const grid = [0, 1, 2, 3].map((index) => {
    const yy = top + index * ((height - top - bottom) / 3);
    const value = high - index * ((high - low) / 3);
    return `<line class="chart-grid" x1="${left}" y1="${yy}" x2="${width - right}" y2="${yy}"/><text class="chart-label" x="0" y="${yy + 4}">${formatCurrency(value, options.currencyCode)}</text>`;
  }).join("");
  const paths = usable.map((item, seriesIndex) => {
    const points = item.points.map((point, index) => `${x(index)},${y(point.value)}`).join(" ");
    const base = height - bottom;
    return `<polygon class="chart-area" fill="${palette[seriesIndex]}" points="${left},${base} ${points} ${x(item.points.length - 1)},${base}"/><polyline class="chart-line" stroke="${palette[seriesIndex]}" points="${points}"/>`;
  }).join("");
  const labels = `<text class="chart-label" x="${left}" y="${height - 4}">${escapeHtml(usable[0].points[0].label || "First bar")}</text><text class="chart-label" text-anchor="end" x="${width - right}" y="${height - 4}">${escapeHtml(usable[0].points.at(-1).label || "Latest bar")}</text>`;
  host.innerHTML = `<svg viewBox="0 0 ${width} ${height}" role="img" aria-label="${escapeHtml(options.label || "Value over time")}">${grid}${paths}${labels}</svg>`;
}

function renderMarketChart(prices) {
  const points = downsample(prices, 160);
  const width = 820, height = 295, left = 4, right = 4, top = 12, bottom = 35;
  const closes = points.map((point) => point.close), volumes = points.map((point) => point.volume);
  const min = Math.min(...closes), max = Math.max(...closes), spread = max - min || Math.max(max * .01, 1), volumeMax = Math.max(...volumes) || 1;
  const x = (index) => left + (index / Math.max(points.length - 1, 1)) * (width - left - right);
  const y = (value) => top + ((max + spread * .08 - value) / (spread * 1.16)) * (height - top - bottom - 37);
  const line = points.map((point, index) => `${x(index)},${y(point.close)}`).join(" ");
  const baseline = height - bottom - 37;
  const bars = points.map((point, index) => {
    const barHeight = (point.volume / volumeMax) * 30;
    return `<rect class="volume-bar" x="${x(index)}" y="${height - bottom - barHeight}" width="${Math.max((width / points.length) - 2, 1)}" height="${barHeight}"/>`;
  }).join("");
  const first = new Date(points[0].timestamp).toLocaleDateString("en-IN", { month: "short", year: "numeric" });
  const last = new Date(points.at(-1).timestamp).toLocaleDateString("en-IN", { month: "short", year: "numeric" });
  $("#market-chart").innerHTML = `<svg viewBox="0 0 ${width} ${height}" role="img" aria-label="Closing price and volume history">${bars}<polygon class="chart-area" fill="#0eaa72" points="${left},${baseline} ${line} ${width - right},${baseline}"/><polyline class="chart-line" stroke="#0eaa72" points="${line}"/><text class="chart-label" x="${left}" y="${height - 5}">${first}</text><text class="chart-label" text-anchor="end" x="${width - right}" y="${height - 5}">${last}</text></svg>`;
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
}

function renderMarket(data) {
  const previousCurrency = state.market?.currency;
  state.market = data;
  const direction = data.price_change >= 0 ? "gained" : "lost";
  const directionClass = data.price_change >= 0 ? "positive" : "negative";
  const range = data.period_high - data.period_low;
  const rangePosition = range ? Math.max(0, Math.min(1, (data.last_close - data.period_low) / range)) : .5;

  $("#ticker-avatar").textContent = data.symbol.replace("^", "")[0] || "M";
  $("#selected-symbol").textContent = data.symbol;
  $("#selected-detail").textContent = `${data.exchange} · ${data.bars} daily bars · ${data.currency}`;
  $("#market-symbol").textContent = data.symbol;
  $("#market-source").textContent = data.provider === "yahoo" ? `Yahoo Finance · ${data.exchange} · daily` : "Validated CSV · daily";
  $("#market-freshness").textContent = `Through ${shortDate(data.end)}`;
  $("#last-close").textContent = formatCurrency(data.last_close, data.currency, 2);
  $("#price-change").textContent = `${percent(data.price_change)} over this period`;
  $("#price-change").className = `change-pill ${directionClass}`;
  $("#market-insight").innerHTML = `<strong>${escapeHtml(data.symbol)} ${direction} ${Math.abs(data.price_change * 100).toFixed(2)}%</strong> across the loaded period. The latest close sits at <strong>${Math.round(rangePosition * 100)}%</strong> of its high-low range, where 100% is the period high.`;
  $("#range-low").textContent = formatCurrency(data.period_low, data.currency, 0);
  $("#range-high").textContent = formatCurrency(data.period_high, data.currency, 0);
  $("#range-position").style.left = `${rangePosition * 100}%`;
  $("#market-stats").innerHTML = `
    <article><span>Period return</span><strong class="${directionClass}">${percent(data.price_change)}</strong><small>First to latest close</small></article>
    <article><span>Average volume</span><strong>${compact.format(data.average_volume)}</strong><small>Shares per day</small></article>
    <article><span>Daily bars</span><strong>${data.bars}</strong><small>${shortDate(data.start)} onward</small></article>
    <article><span>Currency</span><strong>${data.currency}</strong><small>${data.exchange} market</small></article>`;
  renderMarketChart(data.prices);

  $("#backtest-context").textContent = `${data.symbol} · ${data.exchange} · ${data.bars} bars · ${data.currency}`;
  $("#execution-context").textContent = `${data.symbol} · ${data.exchange} · ${data.bars} validated bars`;
  $("#execution-context-detail").textContent = `Latest close ${formatCurrency(data.last_close, data.currency, 2)} on ${shortDate(data.end)}`;
  $("#execution-market-copy").textContent = `Schedule ${data.symbol} using ${formatCurrency(data.last_close, data.currency, 2)} as the current reference price.`;
  $("#execution-reference").textContent = `${data.symbol} · ${formatCurrency(data.last_close, data.currency, 2)}`;
  $("#capital-prefix").textContent = data.currency === "INR" ? "₹" : "$";
  if (previousCurrency !== data.currency) {
    $("#capital").value = data.currency === "INR" ? 1000000 : 100000;
    $("#portfolio-capital").value = data.currency === "INR" ? 1000000 : 100000;
    $("#rms-capital").value = data.currency === "INR" ? 1000000 : 100000;
  }
  const train = Math.max(8, Math.min(60, Math.floor(data.bars * .55)));
  const test = Math.max(3, Math.min(20, Math.floor((data.bars - train) / 2)));
  $("#train-size").value = train;
  $("#test-size").value = test;
}

async function loadMarket({ quiet = false } = {}) {
  const button = $("#load-market");
  setBusy(button, true, state.source === "yahoo" ? "Loading market…" : "Reading CSV…");
  try {
    const data = await post("/api/dataset", dataPayload());
    renderMarket(data);
    await loadRiskSnapshot({ quiet: true });
    if (!quiet) toast(`${data.symbol}: ${data.bars} validated daily bars loaded.`);
  } catch (error) {
    $("#market-chart").innerHTML = `<p class="empty-message">${escapeHtml(error.message)}</p>`;
    toast(`${error.message}${state.source === "yahoo" ? " Try CSV if the network is unavailable." : ""}`, true);
  } finally {
    setBusy(button, false, "");
  }
}

function formatRiskLimit(value, unit) {
  if (unit === "multiple") return `${value.toFixed(2)}×`;
  return `${(value * 100).toFixed(1)}%`;
}

function renderRiskSnapshot(data) {
  const statusCopy = {
    healthy: ["Within limits", "Risk controls are clear", "The selected position is operating inside every configured risk threshold."],
    warning: ["Near limit", "One or more limits need attention", "Review the highlighted utilization before increasing this position."],
    breach: ["Limit breach", "Risk action is required", "At least one configured threshold is breached. Reduce exposure or revise the approved limit."],
  };
  const [label, headline, summary] = statusCopy[data.status];
  const score = Math.round(data.risk_score);
  const ringUse = Math.min(score, 100);
  const riskHero = $("#risk-hero");
  riskHero.className = `panel risk-hero ${data.status}`;
  $("#risk-state").className = `risk-state ${data.status}`;
  $("#risk-status-label").textContent = label;
  $("#risk-headline").textContent = headline;
  $("#risk-summary").textContent = summary;
  $("#risk-as-of").textContent = `${data.symbol} · marked through ${shortDate(data.as_of)}`;
  $("#risk-score").textContent = `${score}%`;
  $("#risk-ring").className = `risk-ring ${data.status}`;
  $("#risk-ring").style.setProperty("--risk-angle", `${ringUse * 3.6}deg`);

  const pnlClass = (value) => value >= 0 ? "positive" : "negative";
  $("#risk-equity").textContent = formatCurrency(data.equity, data.currency, 2);
  $("#risk-day-pnl").textContent = formatCurrency(data.daily_pnl, data.currency, 2);
  $("#risk-day-pnl").className = pnlClass(data.daily_pnl);
  $("#risk-unrealized").textContent = formatCurrency(data.unrealized_pnl, data.currency, 2);
  $("#risk-unrealized").className = pnlClass(data.unrealized_pnl);
  $("#risk-gross").textContent = formatCurrency(data.gross_exposure, data.currency, 0);
  $("#risk-leverage").textContent = `${data.leverage.toFixed(2)}×`;
  $("#risk-var").textContent = formatCurrency(data.var_95, data.currency, 0);

  const breaches = data.limits.filter((limit) => limit.status === "breach").length;
  const warnings = data.limits.filter((limit) => limit.status === "warning").length;
  $("#limit-count").textContent = breaches ? `${breaches} breached` : warnings ? `${warnings} near limit` : "4 limits clear";
  $("#risk-limit-list").innerHTML = data.limits.map((limit) => `
    <div class="limit-row ${limit.status}">
      <div class="limit-name"><strong>${escapeHtml(limit.name)}</strong><small>${formatRiskLimit(limit.current, limit.unit)} used · ${formatRiskLimit(limit.limit, limit.unit)} limit</small></div>
      <div class="limit-track"><i style="width:${Math.min(limit.utilization * 100, 100)}%"></i></div>
      <div class="limit-values">${Math.round(limit.utilization * 100)}% utilized</div>
      <span class="limit-status">${escapeHtml(limit.status)}</span>
    </div>`).join("");

  const exceptions = data.alerts.filter((alert) => alert.severity !== "healthy").length;
  $("#alert-count").textContent = exceptions ? `${exceptions} exception${exceptions === 1 ? "" : "s"}` : "No exceptions";
  $("#risk-alerts").innerHTML = data.alerts.map((alert) => `<article class="risk-alert ${alert.severity}"><i></i><div><strong>${escapeHtml(alert.title)}</strong><small>${escapeHtml(alert.detail)}</small></div></article>`).join("");

  const position = data.position;
  $("#risk-position-body").innerHTML = `<tr>
    <td><strong>${escapeHtml(position.symbol)}</strong></td><td><span class="side-badge">${position.side}</span></td><td>${position.quantity}</td>
    <td>${formatCurrency(position.entry_price, data.currency, 2)}</td><td>${formatCurrency(position.market_price, data.currency, 2)}</td>
    <td>${formatCurrency(position.market_value, data.currency, 2)}</td><td class="${pnlClass(position.unrealized_pnl)}">${formatCurrency(position.unrealized_pnl, data.currency, 2)}</td><td>${(position.position_pct * 100).toFixed(1)}%</td>
  </tr>`;
}

async function loadRiskSnapshot({ quiet = false } = {}) {
  const button = $("#refresh-risk");
  setBusy(button, true, "Calculating risk…");
  try {
    const data = await post("/api/risk-snapshot", riskPayload());
    renderRiskSnapshot(data);
    if (!quiet) toast(`${data.symbol}: RMS recalculated with ${data.status} status.`);
  } catch (error) {
    $("#risk-status-label").textContent = "Risk unavailable";
    $("#risk-headline").textContent = "The RMS calculation could not complete";
    $("#risk-summary").textContent = error.message;
    toast(error.message, true);
  } finally {
    setBusy(button, false, "");
  }
}

function renderSectorOverview(data) {
  const periodNames = { "1mo": "One-month", "3mo": "Three-month", "6mo": "Six-month", "1y": "One-year" };
  $("#sector-title").textContent = `${periodNames[data.period]} return by sector`;
  $("#sector-as-of").textContent = data.as_of ? `Through ${shortDate(data.as_of)}` : "Latest available";
  $("#index-strip").innerHTML = data.indices.map((index) => `
    <article class="index-card"><div><span>${escapeHtml(index.name)}</span><strong>${new Intl.NumberFormat("en-IN", { maximumFractionDigits: 2 }).format(index.last_close)}</strong></div><b class="${index.change >= 0 ? "positive" : "negative"}">${percent(index.change)}</b></article>`).join("") || '<p class="empty-message">Broad-market indices are temporarily unavailable.</p>';

  const leader = data.sectors[0];
  const laggard = data.sectors.at(-1);
  $("#sector-summary").innerHTML = `
    <article><span>Leading sector</span><strong>${leader ? escapeHtml(leader.name) : "—"}</strong><small class="${leader?.change >= 0 ? "positive" : "negative"}">${leader ? percent(leader.change) : "No data"}</small></article>
    <article><span>Market breadth</span><strong>${data.market_breadth == null ? "—" : `${Math.round(data.market_breadth * 100)}% advancing`}</strong><small>${laggard ? `${escapeHtml(laggard.name)} is weakest at ${percent(laggard.change)}` : "No sector data"}</small></article>
    <article><span>Coverage</span><strong>${data.available_instruments} / ${data.requested_instruments}</strong><small>Real Yahoo instruments loaded</small></article>`;

  const maxMagnitude = Math.max(...data.sectors.map((sector) => Math.abs(sector.change)), .001);
  $("#sector-grid").innerHTML = data.sectors.map((sector) => {
    const width = Math.max(Math.abs(sector.change) / maxMagnitude * 49, 1.5);
    const members = sector.constituents.map((member) => `<span class="${member.change >= 0 ? "up" : "down"}" title="${escapeHtml(member.symbol)} ${percent(member.change)}">${escapeHtml(member.symbol.replace(".NS", ""))} ${percent(member.change, 1)}</span>`).join("");
    return `<article class="sector-row">
      <div class="sector-row-head"><div><strong>${escapeHtml(sector.name)}</strong><small>${Math.round(sector.breadth * 100)}% of basket advancing</small></div><b class="${sector.change >= 0 ? "positive" : "negative"}">${percent(sector.change)}</b></div>
      <div class="sector-meter"><i class="${sector.change >= 0 ? "positive" : "negative"}" style="width:${width}%"></i></div>
      <div class="constituent-list">${members}</div>
    </article>`;
  }).join("") || '<p class="empty-message">Sector data is temporarily unavailable.</p>';
  $("#sector-methodology").textContent = `${data.methodology}. ${data.available_instruments} of ${data.requested_instruments} requested instruments loaded. This is a comparison proxy, not an official NSE sector index.`;
}

async function loadSectorOverview({ quiet = false } = {}) {
  $("#sector-as-of").textContent = "Refreshing live data…";
  try {
    const data = await post("/api/market-overview", { period: state.sectorPeriod });
    renderSectorOverview(data);
    if (!quiet) toast(`Indian sector map updated for ${state.sectorPeriod}.`);
  } catch (error) {
    $("#sector-grid").innerHTML = `<p class="empty-message">${escapeHtml(error.message)}</p>`;
    $("#sector-as-of").textContent = "Data unavailable";
    toast(error.message, true);
  }
}

function resultStat(name, value, className = "") {
  return `<div class="result-stat"><span>${name}</span><strong class="${className}">${value}</strong></div>`;
}

function strategyVerdict(run) {
  const excess = run.total_return - run.benchmark_return;
  if (run.fills < 4) return ["Not enough trades", "The strategy produced too few fills for a reliable comparison. Load more history before judging it.", "caution"];
  if (run.total_return > 0 && excess > 0 && (run.sharpe_ratio || 0) > 0) return ["Promising in this sample", `It beat buy-and-hold by ${percent(excess)} and produced a positive risk-adjusted return. Validate it on unseen data next.`, "positive"];
  if (run.total_return > 0 && excess <= 0) return ["Positive, but behind the benchmark", `The strategy gained ${percent(run.total_return)}, but simply holding returned ${percent(run.benchmark_return)}.`, "caution"];
  return ["Did not work in this sample", `The strategy returned ${percent(run.total_return)} after costs. Review the market regime or test a different model.`, "negative"];
}

function renderBacktest(data) {
  const winner = [...data.runs].sort((a, b) => b.total_return - a.total_return)[0];
  const [verdict, explanation, verdictClass] = strategyVerdict(winner);
  const maxMagnitude = Math.max(...data.runs.map((run) => Math.abs(run.total_return)), .001);
  $("#backtest-results").innerHTML = `
    <article class="panel result-hero">
      <div class="decision-banner ${verdictClass}"><span>Decision</span><div><strong>${verdict}</strong><p>${explanation}</p></div><a href="#walk-forward-controls">Validate next ↓</a></div>
      <div class="winner-row"><div class="winner-name"><span class="winner-icon">★</span><div><p>Best result in this run</p><h3>${escapeHtml(titleCase(winner.strategy))}</h3></div></div><div class="winner-return"><span>Total return</span><strong class="${winner.total_return >= 0 ? "positive" : "negative"}">${percent(winner.total_return)}</strong></div></div>
      <div class="result-stats">
        ${resultStat("Ending equity", formatCurrency(winner.ending_equity, data.currency))}
        ${resultStat("Buy & hold", percent(winner.benchmark_return), winner.benchmark_return >= 0 ? "positive" : "negative")}
        ${resultStat("Sharpe ratio", winner.sharpe_ratio == null ? "—" : ratio.format(winner.sharpe_ratio))}
        ${resultStat("Max drawdown", percent(winner.max_drawdown), "negative")}
      </div>
      <div class="result-panel-head"><h3>Equity curve</h3><div class="legend">${data.runs.map((run, index) => `<span><i style="background:${palette[index]}"></i>${escapeHtml(titleCase(run.strategy))}</span>`).join("")}</div></div>
      <div class="data-chart" id="backtest-chart"></div>
    </article>
    <article class="panel comparison-panel">
      <div class="result-panel-head"><div><h3>Compare the evidence</h3><small>Higher return and Sharpe are better; a smaller drawdown is better.</small></div><span class="data-badge">Costs included</span></div>
      <div class="strategy-bars">${data.runs.map((run) => `<div class="strategy-bar-row"><span>${escapeHtml(titleCase(run.strategy))}</span><div class="bar-track"><i class="${run.total_return >= 0 ? "positive" : "negative"}" style="width:${Math.max(Math.abs(run.total_return) / maxMagnitude * 48, 1)}%"></i></div><b class="${run.total_return >= 0 ? "positive" : "negative"}">${percent(run.total_return)}</b></div>`).join("")}</div>
      <div class="strategy-scorecards">${data.runs.map((run) => {
        const excess = run.total_return - run.benchmark_return;
        return `<article><header><strong>${escapeHtml(titleCase(run.strategy))}</strong><span class="${run.total_return >= 0 ? "positive" : "negative"}">${percent(run.total_return)}</span></header><div><span>vs hold<b class="${excess >= 0 ? "positive" : "negative"}">${percent(excess)}</b></span><span>Sharpe<b>${run.sharpe_ratio == null ? "—" : ratio.format(run.sharpe_ratio)}</b></span><span>Drawdown<b class="negative">${percent(run.max_drawdown)}</b></span><span>Fills<b>${run.fills}</b></span></div></article>`;
      }).join("")}</div>
      <details class="result-details"><summary>View volatility and rejected orders <span>⌄</span></summary><table><thead><tr><th>Strategy</th><th>Completed fills</th><th>Annual volatility</th><th>Rejected orders</th></tr></thead><tbody>${data.runs.map((run) => `<tr><td>${escapeHtml(titleCase(run.strategy))}</td><td>${run.fills}</td><td>${percent(run.volatility)}</td><td>${run.rejected_orders}</td></tr>`).join("")}</tbody></table></details>
    </article>`;
  renderLineChart("#backtest-chart", data.runs.map((run) => ({ points: run.equity.map((point) => ({ value: point.value, label: new Date(point.timestamp).toLocaleDateString("en-IN", { month: "short", year: "2-digit" }) })) })), { label: "Strategy equity curves", currencyCode: data.currency });
}

async function runBacktest() {
  const button = $("#run-backtest");
  setBusy(button, true, "Running simulation…");
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
  const positiveFolds = data.folds.filter((fold) => fold.test_return > 0).length;
  const requiredFolds = Math.ceil(data.folds.length * .6);
  const passed = data.performance.total_return > 0 && positiveFolds >= requiredFolds;
  const verdict = passed ? "The result held up better on unseen data" : data.performance.total_return > 0 ? "Positive return, but evidence is mixed" : "The result did not hold up on unseen data";
  const explanation = `${positiveFolds} of ${data.folds.length} unseen windows were positive. ${passed ? "This supports further testing, not automatic deployment." : "Avoid treating the in-sample winner as production-ready."}`;
  $("#research-results").innerHTML = `
    <article class="validation-verdict ${passed ? "positive" : "caution"}"><span>${passed ? "PASS" : "REVIEW"}</span><div><strong>${verdict}</strong><p>${explanation}</p></div></article>
    <div class="metric-ribbon">
      ${resultStat("Out-of-sample return", percent(data.performance.total_return), data.performance.total_return >= 0 ? "positive" : "negative")}
      ${resultStat("Ending equity", formatCurrency(data.performance.ending_equity, data.currency))}
      ${resultStat("Annual volatility", percent(data.performance.annualized_volatility))}
      ${resultStat("Max drawdown", percent(data.performance.max_drawdown), "negative")}
    </div>
    <div class="research-result-grid">
      <article class="panel research-chart-card"><div class="result-panel-head"><h3>Unseen-window equity</h3><span class="data-badge">${data.fills} fills</span></div><div class="data-chart compact-chart" id="research-chart"></div></article>
      <article class="panel fold-card"><div class="result-panel-head"><h3>Fold-by-fold audit</h3><span class="data-badge">${data.folds.length} tests</span></div><div class="fold-list">${data.folds.map((fold) => `<div class="fold-row"><span>${fold.fold}</span><div><b>Lookback ${fold.parameters.lookback} · ${(fold.parameters.threshold * 100).toFixed(1)}% threshold</b><small>${escapeHtml(fold.test)}</small></div><strong class="${fold.test_return >= 0 ? "positive" : "negative"}">${percent(fold.test_return)}</strong></div>`).join("")}</div></article>
    </div>`;
  renderLineChart("#research-chart", [{ points: data.equity.map((point) => ({ value: point.value, label: new Date(point.timestamp).toLocaleDateString("en-IN", { month: "short", year: "2-digit" }) })) }], { label: "Out-of-sample equity", currencyCode: data.currency, height: 215 });
}

async function runWalkForward() {
  const button = $("#run-walk-forward");
  setBusy(button, true, "Validating…");
  try {
    const data = await post("/api/walk-forward", { ...backtestPayload(), train_size: Number($("#train-size").value), test_size: Number($("#test-size").value) });
    renderResearch(data);
    toast(`Walk-forward validation completed across ${data.folds.length} folds.`);
  } catch (error) {
    toast(error.message, true);
  } finally {
    setBusy(button, false, "");
  }
}

function readExecutionField(section, label) {
  const match = section.match(new RegExp(`${label}:\\s+([^\\n]+)`));
  return match ? match[1].trim() : "—";
}

function parseExecutionReports(output) {
  return output.split("EXECUTION REPORT").slice(1).map((section) => ({
    algorithm: readExecutionField(section, "Algorithm"),
    requested: Number(readExecutionField(section, "Requested Qty")),
    executed: Number(readExecutionField(section, "Executed Qty")),
    children: readExecutionField(section, "Child Orders"),
    fills: readExecutionField(section, "Fills"),
    status: readExecutionField(section, "Status"),
    slippage: readExecutionField(section, "Arrival Slippage \\(bps\\)"),
    fees: readExecutionField(section, "Fees"),
  }));
}

async function runExecution() {
  const button = $("#run-execution");
  setBusy(button, true, "Running native engine…");
  $("#output-title").textContent = "Running execution algorithms";
  $("#output-status").textContent = "Working";
  try {
    const data = await post("/api/execution", { ...dataPayload(), execution_quantity: Number($("#execution-quantity").value) });
    const reports = parseExecutionReports(data.output);
    const algorithmPurpose = { TWAP: "Predictable timing", VWAP: "Follow market volume", POV: "Control participation" };
    const comparable = new Set(reports.map((report) => `${report.executed}|${report.slippage}|${report.fees}`)).size === 1;
    const decision = comparable
      ? "All schedules produced the same fill quality"
      : "The schedules produced different execution quality";
    const explanation = comparable
      ? "Simulated liquidity was sufficient for every method, so fill rate, slippage, and fees matched. Choose by scheduling objective, then stress-test thinner liquidity before relying on the result."
      : "Prefer the method that completes the order with lower absolute slippage and acceptable fees, then test it across different liquidity assumptions.";
    $("#output-title").textContent = `${data.symbol} · execution comparison`;
    $("#output-status").textContent = "Native engine complete";
    $("#execution-output").innerHTML = `
      <div class="decision-banner execution-decision"><span>Meaning</span><div><strong>${decision}</strong><p>${explanation}</p></div><em>Real reference ${formatCurrency(data.market_price, data.currency, 2)}</em></div>
      <div class="execution-results">${reports.map((report) => `<article class="execution-result"><header><div><strong>${escapeHtml(report.algorithm)}</strong><small>${algorithmPurpose[report.algorithm] || "Order schedule"}</small></div><span>${escapeHtml(report.status)}</span></header><b>${report.requested ? Math.round(report.executed / report.requested * 100) : 0}%</b><small>of parent quantity filled</small><div class="execution-mini"><div><span>Child orders</span><strong>${escapeHtml(report.children)}</strong></div><div><span>Completed fills</span><strong>${escapeHtml(report.fills)}</strong></div><div><span>Arrival slippage</span><strong>${escapeHtml(report.slippage)} bps</strong></div><div><span>Simulated fees</span><strong>${escapeHtml(report.fees)}</strong></div></div></article>`).join("")}</div>
      <details class="raw-output"><summary>View detailed native-engine report <span>⌄</span></summary><pre>${escapeHtml(data.output)}</pre></details>`;
    toast(`${data.symbol}: execution comparison complete.`);
  } catch (error) {
    $("#output-title").textContent = "Execution could not start";
    $("#output-status").textContent = "Action needed";
    $("#execution-output").innerHTML = `<p class="empty-message">${escapeHtml(error.message)}</p>`;
    toast(error.message, true);
  } finally {
    setBusy(button, false, "");
  }
}

async function runPortfolio() {
  const button = $("#run-portfolio");
  setBusy(button, true, "Valuing position…");
  try {
    const data = await post("/api/portfolio", { ...dataPayload(), portfolio_capital: Number($("#portfolio-capital").value), portfolio_quantity: Number($("#portfolio-quantity").value), commission: Number($("#commission").value) });
    const position = data.positions[0];
    const exposureRatio = data.gross_exposure / data.equity;
    const pnlDirection = data.unrealized_pnl >= 0 ? "gained" : "lost";
    const positionInsight = `The position ${pnlDirection} ${formatCurrency(Math.abs(data.unrealized_pnl), data.currency, 2)} after commission and now uses ${(exposureRatio * 100).toFixed(1)}% of account equity. ${exposureRatio > .35 ? "This is a concentrated position; review it in the RMS above." : "Exposure remains below the dashboard's default 35% concentration limit."}`;
    const stats = [
      ["Entry price", formatCurrency(data.entry_price, data.currency, 2), ""],
      ["Latest close", formatCurrency(data.market_price, data.currency, 2), ""],
      ["Position return", percent(data.position_return), data.position_return >= 0 ? "positive" : "negative"],
      ["Account equity", formatCurrency(data.equity, data.currency, 2), ""],
      ["Unrealized P&L", formatCurrency(data.unrealized_pnl, data.currency, 2), data.unrealized_pnl >= 0 ? "positive" : "negative"],
      ["Gross exposure", formatCurrency(data.gross_exposure, data.currency, 2), ""],
    ];
    $("#output-title").textContent = `${data.symbol} · portfolio snapshot`;
    $("#output-status").textContent = "Real market valuation";
    $("#execution-output").innerHTML = `<div class="decision-banner ${data.unrealized_pnl >= 0 ? "positive" : "caution"}"><span>Impact</span><div><strong>${data.unrealized_pnl >= 0 ? "Position added to account equity" : "Position reduced account equity"}</strong><p>${positionInsight}</p></div><a href="#overview">Review RMS ↑</a></div><div class="portfolio-grid">${stats.map(([name, value, className]) => `<div class="portfolio-stat"><span>${name}</span><strong class="${className}">${value}</strong></div>`).join("")}<div class="positions"><table><thead><tr><th>Position</th><th>Quantity</th><th>Average entry</th><th>Cash remaining</th></tr></thead><tbody><tr><td>${escapeHtml(position.symbol)}</td><td>${position.quantity}</td><td>${formatCurrency(position.average_price, data.currency, 2)}</td><td>${formatCurrency(data.cash, data.currency, 2)}</td></tr></tbody></table></div></div>`;
    toast(`${data.symbol}: position valued through the latest close.`);
  } catch (error) {
    toast(error.message, true);
  } finally {
    setBusy(button, false, "");
  }
}

function updateStrategyGuide() {
  const guides = {
    all: ["3 models", "Momentum follows strength. Mean reversion looks for snap-back. Moving averages follow trend changes."],
    momentum: ["Momentum", "Buys recent strength and sells recent weakness using a five-bar lookback."],
    "mean-reversion": ["Mean reversion", "Looks for statistically stretched prices that may move back toward their recent average."],
    "ma-crossover": ["Moving averages", "Uses a fast and slow average to identify changes in the prevailing price trend."],
  };
  const [name, description] = guides[$("#strategy").value];
  $("#strategy-explainer").innerHTML = `<b>${name}</b><span>${description}</span>`;
}

$$('.source-option').forEach((button) => button.addEventListener("click", () => setSource(button.dataset.source)));
$$('[data-symbol]').forEach((button) => button.addEventListener("click", () => {
  setSource("yahoo");
  $("#symbol").value = button.dataset.symbol;
  $("#exchange").value = button.dataset.exchange;
  loadMarket();
}));
$$('[data-sector-period]').forEach((button) => button.addEventListener("click", () => {
  state.sectorPeriod = button.dataset.sectorPeriod;
  $$('[data-sector-period]').forEach((item) => item.classList.toggle("active", item === button));
  loadSectorOverview();
}));
$("#csv-file").addEventListener("change", async (event) => {
  const file = event.target.files[0];
  state.csvText = file ? await file.text() : "";
  if (file) toast(`${file.name} is ready to load.`);
});
$("#symbol").addEventListener("keydown", (event) => { if (event.key === "Enter") loadMarket(); });
$("#strategy").addEventListener("change", updateStrategyGuide);
$("#load-market").addEventListener("click", () => loadMarket());
$("#refresh-risk").addEventListener("click", () => loadRiskSnapshot());
$("#run-backtest").addEventListener("click", runBacktest);
$("#run-walk-forward").addEventListener("click", runWalkForward);
$("#run-execution").addEventListener("click", runExecution);
$("#run-portfolio").addEventListener("click", runPortfolio);

const sectionObserver = new IntersectionObserver((entries) => {
  const visible = entries.filter((entry) => entry.isIntersecting).sort((a, b) => b.intersectionRatio - a.intersectionRatio)[0];
  if (!visible) return;
  $$(".top-nav a").forEach((link) => link.classList.toggle("active", link.getAttribute("href") === `#${visible.target.id}`));
}, { rootMargin: "-20% 0px -65%", threshold: [0, .2, .5] });
$$('.dashboard-section').forEach((section) => sectionObserver.observe(section));

Promise.all([loadMarket({ quiet: true }), loadSectorOverview({ quiet: true })]);
