-- Compact Wireshark dissector for mesytec MCPD packets.
-- Usage:
--   1) Copy this file to your Wireshark personal plugins folder (~/.config/wireshark/plugins or %APPDATA%\Wireshark\plugins).
--   2) Optionally Decode As UDP port 54321 -> MCPD.

local mcpd = Proto("mcpd", "Mesytec MCPD")

local BUFFER_TYPE = {
    [0x8000] = "Command",
    [0x0001] = "MCPD Data",
    [0x0002] = "MDLL Data",
}

local COMMAND_NAME = {
    [0]  = "Reset",
    [1]  = "StartDAQ",
    [2]  = "StopDAQ",
    [3]  = "ContinueDAQ",
    [4]  = "SetId",
    [5]  = "SetProtoParams",
    [6]  = "SetTiming",
    [7]  = "SetClock",
    [8]  = "SetRunId",
    [9]  = "SetCell",
    [10] = "SetAuxTimer",
    [11] = "SetParam",
    [12] = "GetParams",
    [13] = "SetGain",
    [14] = "SetThreshold",
    [15] = "SetPulser",
    [16] = "SetMpsdMode",
    [17] = "SetDAC",
    [18] = "SendSerial",
    [19] = "ReadSerial",
    [21] = "SetTTLOutputs",
    [22] = "GetBusCapabilities",
    [23] = "SetBusCapabilities",
    [24] = "GetMpsdParams",
    [25] = "SetMpsdTxFormat",
    [26] = "SetMstdGain",
    [36] = "ReadIds",
    [51] = "GetVersion",
    [52] = "ReadPeripheralRegister",
    [53] = "WritePeripheralRegister",
    [60] = "MdllSetTresholds",
    [61] = "MdllSetSpectrum",
    [65] = "MdllSetPulser",
    [66] = "MdllSetTxDataSet",
    [67] = "MdllSetTimingWindow",
    [68] = "MdllSetEnergyWindow",
    [80] = "WriteRegister",
    [81] = "ReadRegister",
}

local f = mcpd.fields
f.buffer_length = ProtoField.uint16("mcpd.buffer_length", "Buffer Length (words)", base.DEC)
f.buffer_type = ProtoField.uint16("mcpd.buffer_type", "Buffer Type", base.HEX, BUFFER_TYPE)
f.header_length = ProtoField.uint16("mcpd.header_length", "Header Length (words)", base.DEC)
f.buffer_number = ProtoField.uint16("mcpd.buffer_number", "Buffer Number", base.DEC)
f.run_id = ProtoField.uint16("mcpd.run_id", "Run ID", base.DEC)
f.device_status = ProtoField.uint8("mcpd.device_status", "Device Status", base.HEX)
f.device_id = ProtoField.uint8("mcpd.device_id", "Device ID", base.DEC)
f.time0 = ProtoField.uint16("mcpd.time0", "Time[0]", base.DEC)
f.time1 = ProtoField.uint16("mcpd.time1", "Time[1]", base.DEC)
f.time2 = ProtoField.uint16("mcpd.time2", "Time[2]", base.DEC)
f.timestamp48 = ProtoField.uint64("mcpd.timestamp48", "Packet Timestamp (48-bit)", base.DEC)
f.cmd = ProtoField.uint16("mcpd.cmd", "Command Word", base.HEX)
f.cmd_num = ProtoField.uint16("mcpd.cmd.num", "Command Number", base.DEC, COMMAND_NAME, 0x00FF)
f.cmd_err = ProtoField.uint16("mcpd.cmd.err", "Command Error", base.DEC, nil, 0xFF00)
f.header_checksum = ProtoField.uint16("mcpd.header_checksum", "Header Checksum", base.HEX)
f.param = ProtoField.uint16("mcpd.param", "Param Word", base.HEX)
f.param48 = ProtoField.uint64("mcpd.param48", "Param Value (48-bit)", base.DEC)
f.data_word = ProtoField.uint16("mcpd.data_word", "Data Word", base.HEX)
f.event_raw = ProtoField.uint64("mcpd.event.raw", "Event Raw (48-bit)", base.HEX)
f.event_type = ProtoField.uint8("mcpd.event.type", "Event Type", base.DEC)
f.event_ts = ProtoField.uint32("mcpd.event.timestamp", "Event Timestamp (19-bit)", base.DEC)
f.event_full_ts = ProtoField.uint64("mcpd.event.full_timestamp", "Event Full Timestamp", base.DEC)
f.neutron_mpsd = ProtoField.uint8("mcpd.event.neutron.mpsd_id", "MPSD ID", base.DEC)
f.neutron_channel = ProtoField.uint8("mcpd.event.neutron.channel", "Channel", base.DEC)
f.neutron_amp = ProtoField.uint16("mcpd.event.neutron.amplitude", "Amplitude", base.DEC)
f.neutron_pos = ProtoField.uint16("mcpd.event.neutron.position", "Position", base.DEC)
f.trigger_id = ProtoField.uint8("mcpd.event.trigger.id", "Trigger ID", base.DEC)
f.trigger_data_id = ProtoField.uint8("mcpd.event.trigger.data_id", "Data ID", base.DEC)
f.trigger_value = ProtoField.uint32("mcpd.event.trigger.value", "Trigger Value", base.DEC)
f.mdll_amp = ProtoField.uint8("mcpd.event.mdll.amplitude", "MDLL Amplitude", base.DEC)
f.mdll_x = ProtoField.uint16("mcpd.event.mdll.x", "MDLL X Position", base.DEC)
f.mdll_y = ProtoField.uint16("mcpd.event.mdll.y", "MDLL Y Position", base.DEC)

local function le_u16_words_to_u48_num(tvb, byte_offset)
    local w0 = tvb(byte_offset + 0, 2):le_uint()
    local w1 = tvb(byte_offset + 2, 2):le_uint()
    local w2 = tvb(byte_offset + 4, 2):le_uint()
    return w0 + (w1 * 65536) + (w2 * 4294967296)
end

local function num_to_u64(n)
    local hi = math.floor(n / 4294967296)
    local lo = n % 4294967296
    return UInt64(hi, lo)
end

local function le_u16_words_to_u48_u64(tvb, byte_offset)
    return num_to_u64(le_u16_words_to_u48_num(tvb, byte_offset))
end

local function field(v, shift, bits)
    local pow_shift = 2 ^ shift
    local pow_bits = 2 ^ bits
    return math.floor(v / pow_shift) % pow_bits
end

local function add_time_fields(tree, tvb, byte_offset)
    tree:add_le(f.time0, tvb(byte_offset + 0, 2))
    tree:add_le(f.time1, tvb(byte_offset + 2, 2))
    tree:add_le(f.time2, tvb(byte_offset + 4, 2))
    tree:add(f.timestamp48, tvb(byte_offset, 6), le_u16_words_to_u48_u64(tvb, byte_offset))
end

local function decode_event(evt_tree, evt48, buffer_type, packet_ts)
    local event_type = field(evt48, 47, 1)
    if event_type == 0 and buffer_type == 0x0002 then
        event_type = 2 -- MDLL neutron special case.
    end

    evt_tree:add(f.event_type, event_type)
    local evt_ts = field(evt48, 0, 19)
    evt_tree:add(f.event_ts, evt_ts)
    evt_tree:add(f.event_full_ts, num_to_u64(packet_ts + evt_ts))

    if event_type == 0 then
        evt_tree:add(f.neutron_mpsd, field(evt48, 44, 3))
        evt_tree:add(f.neutron_channel, field(evt48, 39, 5))
        evt_tree:add(f.neutron_amp, field(evt48, 29, 10))
        evt_tree:add(f.neutron_pos, field(evt48, 19, 10))
        return "Neutron"
    end

    if event_type == 1 then
        evt_tree:add(f.trigger_id, field(evt48, 44, 3))
        evt_tree:add(f.trigger_data_id, field(evt48, 40, 4))
        evt_tree:add(f.trigger_value, field(evt48, 19, 21))
        return "Trigger"
    end

    evt_tree:add(f.mdll_amp, field(evt48, 39, 8))
    evt_tree:add(f.mdll_x, field(evt48, 29, 10))
    evt_tree:add(f.mdll_y, field(evt48, 19, 10))
    return "MdllNeutron"
end

local function dissect_mcpd(tvb, pinfo, tree)
    if tvb:len() < 8 then
        return false
    end

    local buffer_length = tvb(0, 2):le_uint()
    local buffer_type = tvb(2, 2):le_uint()
    local header_length = tvb(4, 2):le_uint()

    if buffer_length == 0 or header_length == 0 then
        return false
    end

    local total_bytes = buffer_length * 2
    if total_bytes > tvb:len() then
        return false
    end

    if header_length > buffer_length then
        return false
    end

    if buffer_type ~= 0x8000 and buffer_type ~= 0x0001 and buffer_type ~= 0x0002 then
        return false
    end

    pinfo.cols.protocol = "MCPD"

    local t = tree:add(mcpd, tvb(0, total_bytes), "Mesytec MCPD")
    t:add_le(f.buffer_length, tvb(0, 2))
    t:add_le(f.buffer_type, tvb(2, 2))
    t:add_le(f.header_length, tvb(4, 2))
    t:add_le(f.buffer_number, tvb(6, 2))

    if buffer_type == 0x8000 then
        if header_length < 10 or total_bytes < 20 then
            return true
        end

        t:add_le(f.cmd, tvb(8, 2))
        t:add_le(f.cmd_num, tvb(8, 2))
        t:add_le(f.cmd_err, tvb(8, 2))
        t:add(f.device_status, tvb(10, 1))
        t:add(f.device_id, tvb(11, 1))
        add_time_fields(t, tvb, 12)
        t:add_le(f.header_checksum, tvb(18, 2))

        local data_words = math.max(0, buffer_length - header_length)
        local data_offset = header_length * 2
        if data_words > 0 and (data_offset + data_words * 2) <= total_bytes then
            local dt = t:add(tvb(data_offset, data_words * 2), "Data")
            for i = 0, data_words - 1 do
                dt:add_le(f.data_word, tvb(data_offset + i * 2, 2))
            end
        end

        local cmd_num = bit32.band(tvb(8, 2):le_uint(), 0x00FF)
        local cmd_name = COMMAND_NAME[cmd_num] or "Unknown"
        pinfo.cols.info = string.format("Command id=%u (%s)", cmd_num, cmd_name)
        return true
    end

    if header_length < 21 or total_bytes < 42 then
        return true
    end

    t:add_le(f.run_id, tvb(8, 2))
    t:add(f.device_status, tvb(10, 1))
    t:add(f.device_id, tvb(11, 1))
    add_time_fields(t, tvb, 12)

    local params_off = 18
    local pt = t:add(tvb(params_off, 24), "Params")
    for i = 0, 3 do
        local po = params_off + i * 6
        local pnode = pt:add(tvb(po, 6), string.format("Param %d", i))
        pnode:add_le(f.param, tvb(po + 0, 2))
        pnode:add_le(f.param, tvb(po + 2, 2))
        pnode:add_le(f.param, tvb(po + 4, 2))
        pnode:add(f.param48, tvb(po, 6), le_u16_words_to_u48_u64(tvb, po))
    end

    local packet_ts = le_u16_words_to_u48_num(tvb, 12)
    local data_words = math.max(0, buffer_length - header_length)
    local event_count = math.floor(data_words / 3)
    local data_offset = header_length * 2
    if data_offset > total_bytes then
        return true
    end
    local et = t:add(tvb(data_offset, math.min(data_words * 2, total_bytes - data_offset)),
        string.format("Events (%d)", event_count))

    for ei = 0, event_count - 1 do
        local eo = data_offset + ei * 6
        if (eo + 6) > total_bytes then
            break
        end
        local evt48 = le_u16_words_to_u48_num(tvb, eo)
        local evt = et:add(tvb(eo, 6), string.format("Event %d", ei))
        evt:add(f.event_raw, tvb(eo, 6), le_u16_words_to_u48_u64(tvb, eo))
        local etype = decode_event(evt, evt48, buffer_type, packet_ts)
        evt:append_text(" (" .. etype .. ")")
    end

    local bt_name = BUFFER_TYPE[buffer_type] or "Data"
    pinfo.cols.info = string.format("%s runId=%u events=%u", bt_name, tvb(8, 2):le_uint(), event_count)
    return true
end

function mcpd.dissector(tvb, pinfo, tree)
    dissect_mcpd(tvb, pinfo, tree)
end

mcpd:register_heuristic("udp", dissect_mcpd)
DissectorTable.get("udp.port"):add(54321, mcpd)
