const channelsElement = document.querySelector("#channels");
const messageElement = document.querySelector("#message");

function showMessage(text, error = false) {
  messageElement.textContent = text;
  messageElement.hidden = false;
  messageElement.className = error ? "message error" : "message success";
}

function channelCard(channel) {
  const card = document.createElement("article");
  card.className = `channel ${channel.enabled ? "active" : ""}`;
  card.innerHTML = `
    <div class="channel-heading"><span class="channel-number">${String(channel.channel).padStart(2, "0")}</span><span class="state">${channel.enabled ? "RUNNING" : "IDLE"}</span></div>
    <div class="readout"><strong>${channel.frequencyHz.toLocaleString()}</strong><span>Hz</span></div>
    <div class="bar"><span style="width:${channel.dutyPercent}%"></span></div>
    <div class="metrics"><span>${channel.dutyPercent}% duty</span><span>${channel.pulseCount.toLocaleString()} pulses</span></div>
    <form class="controls">
      <label>Frequency <input name="frequencyHz" type="number" min="0" max="4294967295" value="${channel.frequencyHz}" required></label>
      <label>Duty <input name="dutyPercent" type="number" min="0" max="100" value="${channel.dutyPercent}" required></label>
      <button type="submit">Apply</button>
    </form>`;
  card.querySelector("form").addEventListener("submit", async (event) => {
    event.preventDefault();
    const form = new FormData(event.target);
    const response = await fetch(`/api/channels/${channel.channel}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ frequencyHz: Number(form.get("frequencyHz")), dutyPercent: Number(form.get("dutyPercent")) })
    });
    if (!response.ok) {
      showMessage((await response.json()).error ?? "Channel update failed", true);
      return;
    }
    showMessage(`Channel ${channel.channel} updated`);
    await refresh();
  });
  return card;
}

async function refresh() {
  const response = await fetch("/api/channels");
  if (!response.ok) {
    showMessage("Device unavailable", true);
    return;
  }
  const channels = await response.json();
  channelsElement.replaceChildren(...channels.map(channelCard));
  document.querySelector("#total").textContent = channels.length;
  document.querySelector("#running").textContent = channels.filter(channel => channel.enabled).length;
  document.querySelector("#pulses").textContent = channels.reduce((sum, channel) => sum + channel.pulseCount, 0).toLocaleString();
}

document.querySelector("#stop").addEventListener("click", async () => {
  const response = await fetch("/api/stop", { method: "POST" });
  showMessage(response.ok ? "All channels stopped" : "Stop failed", !response.ok);
  await refresh();
});

refresh().catch(() => showMessage("Device unavailable", true));
