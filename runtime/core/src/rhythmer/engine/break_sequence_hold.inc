// SPDX-License-Identifier: GPL-3.0-only
// MPC command-gate policy. The pinned FM-1 audio DSP stays unchanged.
// Included inside BreakManipulator::State; audio-thread owned and bounded.
uint8_t sequence_roll_order[16]{};
unsigned sequence_roll_count=0;
void forget_sequence_roll(unsigned key){
    for(unsigned i=0;i<sequence_roll_count;++i)if(sequence_roll_order[i]==key){
        for(unsigned j=i+1;j<sequence_roll_count;++j)sequence_roll_order[j-1]=sequence_roll_order[j];
        --sequence_roll_count;break;
    }
}
void jungle_sequence_note(unsigned command){
    if(command>=32)return;
    six_seq_gates|=uint32_t(1)<<command;
    if(command<16){
        six_seq_hit=int16_t(command);
        // New chops must not cancel a still-gated performance command.
        if(!(six_seq_gates&0xffff0000u)){six_seq_roll=0;six_seq_new=0;}
    }else{
        unsigned key=command-16;forget_sequence_roll(key);
        sequence_roll_order[sequence_roll_count++]=uint8_t(key);
        six_seq_roll=uint8_t(key+1);six_seq_new=1;
    }
    six_seq_release=0;
}
void jungle_sequence_off(unsigned command){
    if(command>=32){six_seq_hit=-1;six_seq_gates=0;sequence_roll_count=0;six_seq_roll=0;six_seq_new=0;}
    else{
        six_seq_gates&=~(uint32_t(1)<<command);
        if(command>=16){
            unsigned key=command-16;forget_sequence_roll(key);
            if(six_seq_roll==key+1){
                six_seq_roll=sequence_roll_count?uint8_t(sequence_roll_order[sequence_roll_count-1]+1):0;
                six_seq_new=six_seq_roll?1:0;
            }
        }
    }
    if(!six_seq_gates){six_seq_hit=-1;six_seq_release=1;}
}
