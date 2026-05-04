module FrequencyCounter (
    input  logic clk,
    input  logic rst_n,
    input  logic cnt_enable,
    input  logic start_transmit,
    input  logic signal_in,
    output logic done,
    output logic freqOut,
    output logic sclk
);

    // ---- State encoding ----
    typedef enum logic [2:0] {
        IDLE      = 3'd0,   // waiting for cnt_enable
        WAIT_EDGE = 3'd1,   // waiting for first rising edge of signal_in
        COUNTING  = 3'd2,   // counting clk cycles over 2048 signal periods
        HOLD      = 3'd3,   // measurement done, waiting for start_transmit
        TRANSMIT  = 3'd4    // shifting stored value out MSB-first
    } state_t;

    state_t state;

    // ---- Double-flop synchronizer + edge detector ----
    logic sync1, sync2, sync_prev;
    logic rising_edge_sig;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sync1     <= 1'b0;
            sync2     <= 1'b0;
            sync_prev <= 1'b0;
        end else begin
            sync1     <= signal_in;   // first flop  (may be metastable)
            sync2     <= sync1;       // second flop (safe)
            sync_prev <= sync2;       // previous value for edge detect
        end
    end

    assign rising_edge_sig = sync2 & ~sync_prev;

    // ---- Datapath registers ----
    logic [31:0] clk_counter;      // counts clk cycles during measurement
    logic [11:0] period_counter;   // counts signal_in rising edges (0-2048)
    logic [31:0] stored_value;     // latched measurement result
    logic [9:0]  sclk_counter;     // divides clk by 1024 for sclk
    logic [4:0]  bit_index;        // which bit to transmit (31 down to 0)

    // ---- FSM + datapath (single always block) ----
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state          <= IDLE;
            clk_counter    <= 32'd0;
            period_counter <= 12'd0;
            stored_value   <= 32'd0;
            sclk_counter   <= 10'd0;
            bit_index      <= 5'd31;
        end else begin
            case (state)
                // ---------------------------------------------------
                IDLE: begin
                    clk_counter    <= 32'd0;
                    period_counter <= 12'd0;
                    sclk_counter   <= 10'd0;
                    bit_index      <= 5'd31;
                    done           <= 1'b0;
                    if (cnt_enable)
                        state <= WAIT_EDGE;
                end

                // ---------------------------------------------------
                WAIT_EDGE: begin
                    clk_counter    <= 32'd0;
                    period_counter <= 12'd0;
                    if (rising_edge_sig)
                        state <= COUNTING;
                end

                // ---------------------------------------------------
                COUNTING: begin
                    clk_counter <= clk_counter + 32'd1;

                    if (rising_edge_sig)
                        period_counter <= period_counter + 12'd1;

                    // 2048 rising edges seen → measurement complete
                    if (period_counter == 12'd2048) begin
                        stored_value <= clk_counter;
                        state        <= HOLD;
                        done         <= 1'b1;
                    end
                end

                // ---------------------------------------------------
                HOLD: begin
                    // freeze stored_value; prepare transmit counters
                    sclk_counter <= 10'd0;
                    bit_index    <= 5'd31;
                    if (start_transmit)
                        state <= TRANSMIT;
                end

                // ---------------------------------------------------
                TRANSMIT: begin
                    sclk_counter <= sclk_counter + 10'd1;

                    if (sclk_counter == 10'd1023) begin
                        // end of one sclk period
                        if (bit_index == 5'd0)
                            state <= IDLE;       // last bit clocked out
                        else
                            bit_index <= bit_index - 5'd1;
                    end
                end

                default: state <= IDLE;
            endcase
        end
    end

    // ---- Output assignments ----
    // sclk = clk/1024, active only during TRANSMIT
    assign sclk    = (state == TRANSMIT) ? sclk_counter[9] : 1'b0;
    // freqOut carries the current bit of the stored count, MSB first
    assign freqOut = (state == TRANSMIT) ? stored_value[bit_index] : 1'b0;

endmodule