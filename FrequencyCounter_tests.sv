`timescale 1ns / 1ps

module FrequencyCounter_tests;

    // ================================================================
    //  Parameters
    // ================================================================
    localparam real CLK_FREQ_HZ   = 12_000_000.0;                       // 12 MHz
    localparam real CLK_PERIOD_NS = 1_000_000_000.0 / CLK_FREQ_HZ;     // ≈83.333 ns
    localparam real HALF_CLK_NS   = CLK_PERIOD_NS / 2.0;               // ≈41.667 ns

    localparam int  NUM_PERIODS   = 2048;
    localparam int  SCLK_DIV      = 1024;
    localparam int  TX_BITS       = 32;

    // ================================================================
    //  DUT signals
    // ================================================================
    logic clk, rst_n, cnt_enable, start_transmit, signal_in;
    logic freqOut, sclk;

    FrequencyCounter dut (
        .clk            (clk),
        .rst_n          (rst_n),
        .cnt_enable     (cnt_enable),
        .start_transmit (start_transmit),
        .signal_in      (signal_in),
        .freqOut        (freqOut),
        .sclk           (sclk)
    );

    // ================================================================
    //  12 MHz clock
    // ================================================================
    initial clk = 1'b0;
    always #(HALF_CLK_NS) clk = ~clk;

    // ================================================================
    //  Configurable square-wave generator for signal_in
    //  – controlled from tests via sig_gen_en / sig_half_period_ns
    // ================================================================
    real  sig_half_period_ns;
    logic sig_gen_en;

    initial begin
        sig_gen_en          = 1'b0;
        sig_half_period_ns  = 5000.0;       // default (100 KHz)
    end

    always begin
        wait (sig_gen_en);                  // sleep until enabled
        signal_in = 1'b0;
        while (sig_gen_en) begin
            #(sig_half_period_ns);
            signal_in = ~signal_in;
        end
        signal_in = 1'b0;                  // clean up on disable
    end

    // ================================================================
    //  Self-checking scoreboard
    // ================================================================
    int pass_cnt = 0, fail_cnt = 0;

    task automatic check(input string name, input logic cond);
        if (cond) begin
            pass_cnt++;
            $display("  [PASS] %s", name);
        end else begin
            fail_cnt++;
            $display("  [FAIL] %s", name);
        end
    endtask

    // ================================================================
    //  Utility tasks
    // ================================================================

    // Hard reset – all controls low, signal generator off
    task automatic do_reset();
        rst_n          = 1'b0;
        cnt_enable     = 1'b0;
        start_transmit = 1'b0;
        signal_in      = 1'b0;
        sig_gen_en     = 1'b0;
        repeat (10) @(posedge clk);
        rst_n = 1'b1;
        repeat (5)  @(posedge clk);
    endtask

    // Run a full measurement with the given signal half-period (ns).
    // Returns with the DUT in HOLD and signal_in stopped at 0.
    task automatic run_measurement(input real half_period);
        sig_half_period_ns = half_period;
        sig_gen_en         = 1'b1;

        // Let square wave stabilise for a few periods
        #(half_period * 6.0);

        // Start measurement
        @(posedge clk);
        cnt_enable = 1'b1;

        // Wait long enough for 2048 full signal periods + margin
        #(half_period * 2.0 * (NUM_PERIODS + 20));

        cnt_enable = 1'b0;

        // Shut down the generator and let the always-block settle
        sig_gen_en = 1'b0;
        #(half_period + 200);
        repeat (5) @(posedge clk);
    endtask

    // Pulse start_transmit, then capture 32 serial bits on sclk edges.
    task automatic read_serial(output logic [31:0] value);
        value = 32'd0;

        @(posedge clk);
        start_transmit = 1'b1;
        @(posedge clk);
        start_transmit = 1'b0;

        for (int i = TX_BITS - 1; i >= 0; i--) begin
            @(posedge sclk);
            #1;                             // combinational settle
            value[i] = freqOut;
        end

        // Let transmission finish cleanly
        @(posedge clk);
        wait (sclk == 1'b0);
        repeat (10) @(posedge clk);
    endtask

    // ================================================================
    //  Main test sequence
    // ================================================================
    initial begin
        $timeformat(-9, 1, " ns", 12);
        $display("");
        $display("==========================================================");
        $display("  FrequencyCounter – Self-Checking Testbench");
        $display("  CLK  = %.3f MHz", CLK_FREQ_HZ / 1.0e6);
        $display("==========================================================");
        $display("");

        // =============================================================
        //  TEST 1 – Reset behaviour
        // =============================================================
        $display("--- Test 1: Reset Behaviour ---");
        do_reset();
        check("freqOut = 0 after reset", freqOut === 1'b0);
        check("sclk    = 0 after reset", sclk    === 1'b0);
        $display("");

        // =============================================================
        //  TEST 2 – 100 KHz signal (fast simulation sanity check)
        //           Expected count: 12 MHz / 100 KHz × 2048 = 245 760
        // =============================================================
        $display("--- Test 2: 100 KHz Signal Measurement ---");
        begin
            real          half_ns  = 1.0e9 / 100_000.0 / 2.0;  // 5 000 ns
            logic [31:0]  serial_val;
            int           expected = 245760;
            int           tol      = 50;

            do_reset();
            run_measurement(half_ns);

            check("DUT in HOLD after measurement", dut.state == 3);

            read_serial(serial_val);

            $display("    Expected ~%0d  (tolerance +/-%0d)", expected, tol);
            $display("    Received  %0d", serial_val);
            $display("    Delta     %0d",
                     (serial_val > expected) ? int'(serial_val) - expected
                                             : expected - int'(serial_val));

            check("Count within tolerance",
                  serial_val >= (expected - tol) &&
                  serial_val <= (expected + tol));
            check("Serial readback == stored_value",
                  serial_val == dut.stored_value);
        end
        $display("");

        // =============================================================
        //  TEST 3 – Realistic 2.2 KHz signal
        //           Expected count: 12 MHz / 2.2 KHz × 2048 ≈ 11 170 909
        // =============================================================
        $display("--- Test 3: 2.2 KHz Signal Measurement ---");
        $display("    (Simulating 2048 periods at 2.2 KHz – this takes a moment)");
        begin
            real          half_ns  = 1.0e9 / 2_200.0 / 2.0;    // ≈227 272.7 ns
            logic [31:0]  serial_val;
            int           expected = 11170909;
            int           tol      = 100;

            do_reset();
            run_measurement(half_ns);

            check("DUT in HOLD after 2.2 KHz measurement", dut.state == 3);

            read_serial(serial_val);

            $display("    Expected ~%0d  (tolerance +/-%0d)", expected, tol);
            $display("    Received  %0d", serial_val);
            $display("    Delta     %0d",
                     (serial_val > expected) ? int'(serial_val) - expected
                                             : expected - int'(serial_val));

            check("2.2 KHz count within tolerance",
                  serial_val >= (expected - tol) &&
                  serial_val <= (expected + tol));
            check("Serial readback == stored_value",
                  serial_val == dut.stored_value);
        end
        $display("");

        // =============================================================
        //  TEST 4 – FSM should stay in WAIT_EDGE until a real rising
        //           edge arrives, even if signal_in is already high
        // =============================================================
        $display("--- Test 4: Waits for First Rising Edge ---");
        begin
            do_reset();

            // Hold signal_in HIGH – the synchroniser will see a constant 1
            signal_in = 1'b1;
            repeat (10) @(posedge clk);

            cnt_enable = 1'b1;
            repeat (20) @(posedge clk);

            check("WAIT_EDGE while signal stuck high", dut.state == 1);

            // Now create a clean falling → rising transition
            signal_in = 1'b0;
            repeat (10) @(posedge clk);      // let sync capture the low
            signal_in = 1'b1;
            repeat (10) @(posedge clk);      // let sync see the rising edge

            check("Transitions to COUNTING after edge", dut.state == 2);

            cnt_enable = 1'b0;
            signal_in  = 1'b0;
        end
        $display("");

        // =============================================================
        //  TEST 5 – Verify stored value is frozen in HOLD
        // =============================================================
        $display("--- Test 5: Value Holds Until start_transmit ---");
        begin
            real          half_ns = 1.0e9 / 100_000.0 / 2.0;
            logic [31:0]  snap1, snap2;

            do_reset();
            run_measurement(half_ns);

            snap1 = dut.stored_value;
            repeat (50_000) @(posedge clk);  // long idle – no start_transmit
            snap2 = dut.stored_value;

            check("stored_value unchanged after 50 k clocks", snap1 == snap2);
            check("State still HOLD",                         dut.state == 3);
            check("freqOut = 0 in HOLD",                      freqOut === 1'b0);
            check("sclk    = 0 in HOLD",                      sclk    === 1'b0);
        end
        $display("");

        // =============================================================
        //  TEST 6 – Verify sclk period = clk ÷ 1024
        // =============================================================
        $display("--- Test 6: sclk Period Verification (clk / 1024) ---");
        begin
            real          half_ns = 1.0e9 / 100_000.0 / 2.0;
            realtime      t1, t2;
            real          meas_period, exp_period;

            do_reset();
            run_measurement(half_ns);

            @(posedge clk);
            start_transmit = 1'b1;
            @(posedge clk);
            start_transmit = 1'b0;

            // Measure time between two consecutive sclk rising edges
            @(posedge sclk);  t1 = $realtime;
            @(posedge sclk);  t2 = $realtime;

            meas_period = t2 - t1;
            exp_period  = CLK_PERIOD_NS * SCLK_DIV;

            $display("    Expected sclk period = %.1f ns", exp_period);
            $display("    Measured sclk period = %.1f ns", meas_period);

            check("sclk period matches clk / 1024",
                  meas_period > (exp_period - 2.0) &&
                  meas_period < (exp_period + 2.0));

            // Let transmit finish
            wait (dut.state == 0);
            repeat (5) @(posedge clk);
        end
        $display("");

        // =============================================================
        //  TEST 7 – Verify MSB-first bit ordering, bit by bit
        // =============================================================
        $display("--- Test 7: MSB-First Bit Ordering ---");
        begin
            real          half_ns = 1.0e9 / 100_000.0 / 2.0;
            logic [31:0]  expected_val, serial_val;
            int           bit_errors;

            do_reset();
            run_measurement(half_ns);

            expected_val = dut.stored_value;  // snapshot before transmit
            serial_val   = 32'd0;
            bit_errors   = 0;

            @(posedge clk);
            start_transmit = 1'b1;
            @(posedge clk);
            start_transmit = 1'b0;

            for (int i = TX_BITS - 1; i >= 0; i--) begin
                @(posedge sclk);
                #1;
                serial_val[i] = freqOut;
                if (freqOut !== expected_val[i]) begin
                    $display("    Bit[%0d]: expected %b  got %b", i,
                             expected_val[i], freqOut);
                    bit_errors++;
                end
            end

            $display("    stored_value  = 0x%08h  (%0d)", expected_val, expected_val);
            $display("    serial output = 0x%08h  (%0d)", serial_val,   serial_val);

            check("All 32 bits received correctly",    bit_errors == 0);
            check("Reconstructed value matches stored", serial_val == expected_val);

            wait (dut.state == 0);
            repeat (5) @(posedge clk);
        end
        $display("");

        // =============================================================
        //  TEST 8 – Back-to-back measurements (repeatability)
        // =============================================================
        $display("--- Test 8: Back-to-Back Measurements ---");
        begin
            real          half_ns  = 1.0e9 / 100_000.0 / 2.0;
            logic [31:0]  val1, val2;
            int           expected = 245760;
            int           tol      = 50;
            int           delta;

            do_reset();

            // First measurement + readout
            run_measurement(half_ns);
            read_serial(val1);

            repeat (100) @(posedge clk);     // small gap

            // Second measurement + readout
            run_measurement(half_ns);
            read_serial(val2);

            delta = (val1 > val2) ? int'(val1) - int'(val2)
                                  : int'(val2) - int'(val1);

            $display("    Measurement 1 = %0d", val1);
            $display("    Measurement 2 = %0d", val2);
            $display("    Delta         = %0d", delta);

            check("Measurement 1 in expected range",
                  val1 >= (expected - tol) && val1 <= (expected + tol));
            check("Measurement 2 in expected range",
                  val2 >= (expected - tol) && val2 <= (expected + tol));
            check("Measurements consistent (delta < 20)", delta < 20);
        end
        $display("");

        // =============================================================
        //  Summary
        // =============================================================
        $display("==========================================================");
        $display("  Results: %0d / %0d passed", pass_cnt, pass_cnt + fail_cnt);
        if (fail_cnt == 0)
            $display("  *** ALL TESTS PASSED ***");
        else
            $display("  *** %0d TEST(S) FAILED ***", fail_cnt);
        $display("==========================================================");
        $display("");

        $stop;
    end

endmodule
