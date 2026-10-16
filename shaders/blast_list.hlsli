// Needs frame_constants.hlsli with PYROCLAST_REALISM defined.
#pragma once

uint blast_count()
{
    return min(frame.blast_state.x, maximum_blasts);
}

uint blast_slot(uint index)
{
    return (frame.blast_slots[index / 4] >> ((index % 4) * 8)) & 0xff;
}
