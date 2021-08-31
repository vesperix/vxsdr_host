local vxsdr = Proto("vxsdr", "VXSDR Protocol")

local vxsdr_port = 1030
local vxsdr_min_len = 8

local packet_types = {
    [0x00] = "TX_SIGNAL_DATA",
    [0x01] = "RX_SIGNAL_DATA",
    [0x02] = "DEVICE_CMD",
    [0x03] = "TX_RADIO_CMD",
    [0x04] = "RX_RADIO_CMD",
    [0x0A] = "DEVICE_CMD_ERR",
    [0x0B] = "TX_RADIO_CMD_ERR",
    [0x0C] = "RX_RADIO_CMD_ERR",
    [0x0D] = "DEVICE_CMD_RSP",
    [0x0E] = "TX_RADIO_CMD_RSP",
    [0x0F] = "RX_RADIO_CMD_RSP",
    [0x80] = "TX_SIGNAL_DATA_ACK",
    [0x81] = "RX_SIGNAL_DATA_ACK",
    [0x82] = "DEVICE_CMD_ACK",
    [0x83] = "TX_RADIO_CMD_ACK",
    [0x84] = "RX_RADIO_CMD_ACK"
}

local device_cmds = {
    [0x00] = "HELLO",
    [0x01] = "SET_TIME_NOW",
    [0x02] = "SET_TIME_NEXT_PPS",
    [0x03] = "GET_TIME",
    [0x04] = "GET_STATUS",
    [0x05] = "CLEAR_STATUS",
    [0x06] = "GET_BUFFER_INFO",
    [0x07] = "GET_BUFFER_USE",
    [0x08] = "OPEN_SESSION",  -- unimplemented
    [0x09] = "CLOSE_SESSION", --unimplemented
    -- 0x0A-0x0F reserved
    [0x10] = "GET_TRANSPORT_INFO", -- unimplemented
    [0x11] = "GET_TRANSPORT_ADDR",
    [0x12] = "GET_TRANSPORT_PKTSIZ",
    -- 0x12-0x1F reserved
    [0x21] = "SET_TRANSPORT_ADDR",
    [0x22] = "SET_TRANSPORT_PKTSIZ",
    [0x23] = "SAVE_TRANSPORT_ADDR",
    -- 0x22-0x2F reserved
    [0x30] = "GET_CHANNEL_INFO",  -- unimplemented
    -- 0x31-0x4F reserved
    [0x50] = "GET_TIMING_INFO",   -- unimplemented
    [0x51] = "GET_TIMING_STATUS",
    [0x52] = "GET_TIMING_REF",  -- unimplemented
    [0x53] = "SET_TIMING_REF",  -- unimplemented
    [0x60] = "READ_DAC_REG",
    [0x61] = "WRITE_DAC_REG",
    [0x62] = "READ_ADC_REG",
    [0x63] = "WRITE_ADC_REG",
    -- 0x64-0xFD reserved
    [0xFE] = "STOP",  -- unimplemented
    [0xFF] = "RESET"  -- unimplemented
}

local device_cmd_rsps = {
    [0x00] = "HELLO_RSP",
    [0x03] = "GET_TIME_RSP",
    [0x04] = "GET_STATUS_RSP",
    [0x06] = "GET_BUFFER_INFO_RSP",
    [0x07] = "GET_BUFFER_USE_RSP",
    [0x08] = "OPEN_SESSION_RSP",  -- unimplemented
    [0x09] = "CLOSE_SESSION_RSP", --unimplemented
    -- 0x0A-0x0F reserved
    [0x10] = "GET_TRANSPORT_INFO_RSP", -- unimplemented
    [0x11] = "GET_TRANSPORT_ADDR_RSP",
    [0x12] = "GET_TRANSPORT_PKTSIZ_RSP",
    [0x30] = "GET_CHANNEL_INFO_RSP",  -- unimplemented
    -- 0x31-0x4F reserved
    [0x50] = "GET_TIMING_INFO_RSP",   -- unimplemented
    [0x51] = "GET_TIMING_STATUS_RSP",
    [0x52] = "GET_TIMING_REF_RSP",  -- unimplemented
    [0x60] = "READ_DAC_REG_RSP",
    [0x62] = "READ_ADC_REG_RSP"
}

local radio_cmds = {
    [0x01] = "START_NOW_UNTIL_STOP",
    [0x02] = "START_AT_TIME_UNTIL_STOP",
    [0x03] = "STOP_NOW",
    [0x04] = "START_NOW_FOR_N_SAMPLES",
    [0x05] = "START_AT_TIME_FOR_N_SAMPLES",
    -- 0x06-0x0F reserved
    [0x10] = "GET_RF_INFO",  -- unimplemented
    [0x11] = "GET_RF_FREQ",  -- unimplemented
    [0x12] = "GET_RF_GAIN",  -- unimplemented
    [0x13] = "GET_SAMPLE_RATE",
    [0x14] = "GET_RF_BW",  -- unimplemented
    [0x15] = "GET_RF_ENABLED",  -- unimplemented
    [0x16] = "GET_RF_PORT",  -- unimplemented
    -- 0x17-0x20 reserved
    [0x21] = "SET_RF_FREQ",
    [0x22] = "SET_RF_GAIN",
    [0x23] = "SET_SAMPLE_RATE",
    [0x24] = "SET_RF_BW",  -- unimplemented
    [0x25] = "SET_RF_ENABLED",
    [0x26] = "SET_RF_PORT"
    -- 0x27-0xFF reserved
}

local radio_cmd_rsps = {
    [0x10] = "GET_RF_INFO_RSP",  -- unimplemented
    [0x11] = "GET_RF_FREQ_RSP",  -- unimplemented
    [0x12] = "GET_RF_GAIN_RSP",  -- unimplemented
    [0x13] = "GET_SAMPLE_RATE_RSP",
    [0x14] = "GET_RF_BW_RSP",  -- unimplemented
    [0x15] = "GET_RF_ENABLED_RSP",  -- unimplemented
    [0x16] = "GET_RF_PORT_RSP"  -- unimplemented
}

local errors = {
    [1] = "BAD_CMD",
    [2] = "BUSY",
    [3] = "NO_SUCH_SUBDEVICE",
    [4] = "TIMEOUT",
    [5] = "BAD_PARAMETER"
}


local pf_packet_type       = ProtoField.uint8("vxsdr.packet_type", "Packet Type", base.HEX, packet_types)
local pf_command           = ProtoField.uint8("vxsdr.command", "Command", base.HEX)
local pf_target_subdevice  = ProtoField.uint8("vxsdr.target_subdevice", "Target Subdevice", base.DEC)
local pf_flags             = ProtoField.uint8("vxsdr.flags", "Flags", base.HEX)
local pf_packet_size       = ProtoField.uint16("vxsdr.packet_size", "Packet Size", base.DEC)
local pf_sequence_counter  = ProtoField.uint16("vxsdr.sequence_counter", "Sequence Counter", base.DEC)

local pf_device_command     = ProtoField.uint8("vxsdr.device_command", "Device Command", base.HEX, device_cmds)
local pf_device_command_rsp = ProtoField.uint8("vxsdr.device_command_rsp", "Device Command Response", base.HEX, device_cmd_rsps)
local pf_radio_command      = ProtoField.uint8("vxsdr.radio_command", "Radio Command", base.HEX, radio_cmds)
local pf_radio_command_rsp  = ProtoField.uint8("vxsdr.device_command", "Radio Command Response", base.HEX, radio_cmd_rsps)
local pf_command_error      = ProtoField.uint8("vxsdr.error", "Command Error", base.HEX, errors)

vxsdr.fields = { pf_packet_type, pf_command, pf_target_subdevice, pf_flags, pf_packet_size, pf_sequence_counter,
                 pf_device_command, pf_radio_command, pf_error, pf_device_command_rsp, pf_radio_command_rsp, pf_command_error }

function vxsdr.dissector(tvbuf, pktinfo, root)

    pktinfo.cols.protocol:set("VXSDR")
    local pkt_len = tvbuf:reported_length_remaining()
    local pkt_type = tvbuf:range(0, 1):uint()

    local tree = root:add(vxsdr, tvbuf:range(0, pktlen))

    if pkt_len < vxsdr_min_len then
        return
    end

    tree:add(   pf_packet_type,      tvbuf:range(0, 1))
    if pkt_type == 0x02 then
        tree:add(   pf_device_command,         tvbuf:range(1, 1))
    elseif pkt_type == 0x0D then
        tree:add(   pf_device_command_rsp,     tvbuf:range(1, 1))
    elseif pkt_type == 0x03 or pkt_type == 0x04 then
        tree:add(   pf_radio_command,          tvbuf:range(1, 1))
    elseif pkt_type == 0x0E or pkt_type == 0x0F then
        tree:add(   pf_radio_command_rsp,      tvbuf:range(1, 1))
    elseif pkt_type == 0x0A or pkt_type == 0x0B or pkt_type == 0x0C then
        tree:add(   pf_command_error,          tvbuf:range(1, 1))
    else
        tree:add(   pf_command,          tvbuf:range(1, 1))
    end
    tree:add(   pf_target_subdevice, tvbuf:range(2, 1))
    tree:add(   pf_flags,            tvbuf:range(3, 1))
    tree:add_le(pf_packet_size,      tvbuf:range(4, 2))
    tree:add_le(pf_sequence_counter, tvbuf:range(6, 2))
end

local udp_port = DissectorTable.get("udp.port")
udp_port:add(vxsdr_port, vxsdr)
