// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

import { OPERATION } from "../../src/js/device.mjs";

export const view = bytes => new DataView(Uint8Array.from(bytes).buffer);

export const identityBytes = (overrides = {}) => {
  const bytes = [1, 3, 0, 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 2, 24, 3, 0x1f];
  Object.entries(overrides).forEach(([index, value]) => { bytes[index] = value; });
  return bytes;
};

export const configurationBytes = () => [
  1, 0, 1, 2, 0, 0, 1, 0x20, 0, 1, 2, 0, 0, 1, 0x20, 1, 1, 0x80, 0xd1, 0xf0, 0x08,
];

export function fakeDevice({ acceptSet = true, pendingPolls = 1, capabilities = 3, actionStatus = 0,
  applyChannelReadback = false } = {}) {
  return {
    vendorId: 0x2e8a, productId: 0x116c, collections: [{ usagePage: 0xff00, usage: 1 }], opened: false,
    sent: [], result: [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], pending: 0,
    appliedChannels: new Map(),
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    async receiveFeatureReport(id) {
      if (id === 1) return view(identityBytes({ 16: capabilities }));
      if (id === 3) return view([1, 0, 0x80, 0, 0, 0, 0x20, 4, 0]);
      if (id === 4) return view([1, 123, 0, 0, 0, 0x78, 0xEC, 0xFF, 0xFF, 100, 0, 0xC4, 9]);
      if (id === 5) return view([1, ...Array.from({ length: 24 }, (_, channel) => 47 - channel), 2, 3, 255]);
      if (id === 2) return view(configurationBytes());
      if (id === 20) {
        const result = [...this.result];
        if (this.pending > 0) { this.pending--; result[3] = 6; }
        return view(result);
      }
      const bytes = [1];
      const bank = id - 10;
      for (let index = 0; index < 8; index++) {
        const applied = this.appliedChannels.get(bank * 8 + index);
        const frequency = applied?.freq_hz ?? 100 * index;
        const duty = applied?.duty ?? index;
        bytes.push(frequency & 0xff, frequency >>> 8 & 0xff, frequency >>> 16 & 0xff,
          frequency >>> 24 & 0xff, duty);
      }
      return view(bytes);
    },
    async sendFeatureReport(id, data) {
      if (id !== 20) throw new Error(`Unexpected report ID ${id}`);
      this.sent.push([...data]);
      if (acceptSet) {
        this.result = [1, data[1], data[2], actionStatus, 0, 0, 0, 0, 0, 0, 0, 0];
        if (applyChannelReadback && data[2] === OPERATION.SET_CHANNEL) {
          const freqHz = data[4] + (data[5] << 8) + (data[6] << 16) + (data[7] << 24);
          this.appliedChannels.set(data[3], { freq_hz: freqHz >>> 0, duty: data[8] });
        }
        this.pending = pendingPolls;
      }
    },
  };
}
