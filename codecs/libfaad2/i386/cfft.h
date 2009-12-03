static void RENAME(passf2pos)(const uint16_t ido, const uint16_t l1, const complex_t *cc,
                      complex_t *ch, const complex_t *wa)
{
    uint16_t i, k, ah, ac;

    if (ido == 1)
    {
        for (k = 0; k < l1; k++)
        {
            ah = 2*k;
            ac = 4*k;

            RE(ch[ah])    = RE(cc[ac]) + RE(cc[ac+1]);
            RE(ch[ah+l1]) = RE(cc[ac]) - RE(cc[ac+1]);
            IM(ch[ah])    = IM(cc[ac]) + IM(cc[ac+1]);
            IM(ch[ah+l1]) = IM(cc[ac]) - IM(cc[ac+1]);
        }
    } else {
        for (k = 0; k < l1; k++)
        {
            ah = k*ido;
            ac = 2*k*ido;

            for (i = 0; i < ido; i++)
            {
                complex_t t2;

                RE(ch[ah+i]) = RE(cc[ac+i]) + RE(cc[ac+i+ido]);
                RE(t2)       = RE(cc[ac+i]) - RE(cc[ac+i+ido]);

                IM(ch[ah+i]) = IM(cc[ac+i]) + IM(cc[ac+i+ido]);
                IM(t2)       = IM(cc[ac+i]) - IM(cc[ac+i+ido]);

#if 1
                ComplexMult(&IM(ch[ah+i+l1*ido]), &RE(ch[ah+i+l1*ido]),
                    IM(t2), RE(t2), RE(wa[i]), IM(wa[i]));
#else
                ComplexMult(&RE(ch[ah+i+l1*ido]), &IM(ch[ah+i+l1*ido]),
                    RE(t2), IM(t2), RE(wa[i]), IM(wa[i]));
#endif
            }
        }
    }
}

static void RENAME(passf2neg)(const uint16_t ido, const uint16_t l1, const complex_t *cc,
                      complex_t *ch, const complex_t *wa)
{
    uint16_t i, k, ah, ac;

    if (ido == 1)
    {
        for (k = 0; k < l1; k++)
        {
            ah = 2*k;
            ac = 4*k;

            RE(ch[ah])    = RE(cc[ac]) + RE(cc[ac+1]);
            RE(ch[ah+l1]) = RE(cc[ac]) - RE(cc[ac+1]);
            IM(ch[ah])    = IM(cc[ac]) + IM(cc[ac+1]);
            IM(ch[ah+l1]) = IM(cc[ac]) - IM(cc[ac+1]);
        }
    } else {
        for (k = 0; k < l1; k++)
        {
            ah = k*ido;
            ac = 2*k*ido;

            for (i = 0; i < ido; i++)
            {
                complex_t t2;

                RE(ch[ah+i]) = RE(cc[ac+i]) + RE(cc[ac+i+ido]);
                RE(t2)       = RE(cc[ac+i]) - RE(cc[ac+i+ido]);

                IM(ch[ah+i]) = IM(cc[ac+i]) + IM(cc[ac+i+ido]);
                IM(t2)       = IM(cc[ac+i]) - IM(cc[ac+i+ido]);

#if 1
                ComplexMult(&RE(ch[ah+i+l1*ido]), &IM(ch[ah+i+l1*ido]),
                    RE(t2), IM(t2), RE(wa[i]), IM(wa[i]));
#else
                ComplexMult(&IM(ch[ah+i+l1*ido]), &RE(ch[ah+i+l1*ido]),
                    IM(t2), RE(t2), RE(wa[i]), IM(wa[i]));
#endif
            }
        }
    }
}

static void RENAME(passf3)(const uint16_t ido, const uint16_t l1, const complex_t *cc,
                   complex_t *ch, const complex_t *wa1, const complex_t *wa2,
                   const int8_t isign)
{
    static real_t taur = FRAC_CONST(-0.5);
    static real_t taui = FRAC_CONST(0.866025403784439);
    uint16_t i, k, ac, ah;
    complex_t c2, c3, d2, d3, t2;

    if (ido == 1)
    {
        if (isign == 1)
        {
            for (k = 0; k < l1; k++)
            {
                ac = 3*k+1;
                ah = k;

                RE(t2) = RE(cc[ac]) + RE(cc[ac+1]);
                IM(t2) = IM(cc[ac]) + IM(cc[ac+1]);
                RE(c2) = RE(cc[ac-1]) + MUL_F(RE(t2),taur);
                IM(c2) = IM(cc[ac-1]) + MUL_F(IM(t2),taur);

                RE(ch[ah]) = RE(cc[ac-1]) + RE(t2);
                IM(ch[ah]) = IM(cc[ac-1]) + IM(t2);

                RE(c3) = MUL_F((RE(cc[ac]) - RE(cc[ac+1])), taui);
                IM(c3) = MUL_F((IM(cc[ac]) - IM(cc[ac+1])), taui);

                RE(ch[ah+l1]) = RE(c2) - IM(c3);
                IM(ch[ah+l1]) = IM(c2) + RE(c3);
                RE(ch[ah+2*l1]) = RE(c2) + IM(c3);
                IM(ch[ah+2*l1]) = IM(c2) - RE(c3);
            }
        } else {
            for (k = 0; k < l1; k++)
            {
                ac = 3*k+1;
                ah = k;

                RE(t2) = RE(cc[ac]) + RE(cc[ac+1]);
                IM(t2) = IM(cc[ac]) + IM(cc[ac+1]);
                RE(c2) = RE(cc[ac-1]) + MUL_F(RE(t2),taur);
                IM(c2) = IM(cc[ac-1]) + MUL_F(IM(t2),taur);

                RE(ch[ah]) = RE(cc[ac-1]) + RE(t2);
                IM(ch[ah]) = IM(cc[ac-1]) + IM(t2);

                RE(c3) = MUL_F((RE(cc[ac]) - RE(cc[ac+1])), taui);
                IM(c3) = MUL_F((IM(cc[ac]) - IM(cc[ac+1])), taui);

                RE(ch[ah+l1]) = RE(c2) + IM(c3);
                IM(ch[ah+l1]) = IM(c2) - RE(c3);
                RE(ch[ah+2*l1]) = RE(c2) - IM(c3);
                IM(ch[ah+2*l1]) = IM(c2) + RE(c3);
            }
        }
    } else {
        if (isign == 1)
        {
            for (k = 0; k < l1; k++)
            {
                for (i = 0; i < ido; i++)
                {
                    ac = i + (3*k+1)*ido;
                    ah = i + k * ido;

                    RE(t2) = RE(cc[ac]) + RE(cc[ac+ido]);
                    RE(c2) = RE(cc[ac-ido]) + MUL_F(RE(t2),taur);
                    IM(t2) = IM(cc[ac]) + IM(cc[ac+ido]);
                    IM(c2) = IM(cc[ac-ido]) + MUL_F(IM(t2),taur);

                    RE(ch[ah]) = RE(cc[ac-ido]) + RE(t2);
                    IM(ch[ah]) = IM(cc[ac-ido]) + IM(t2);

                    RE(c3) = MUL_F((RE(cc[ac]) - RE(cc[ac+ido])), taui);
                    IM(c3) = MUL_F((IM(cc[ac]) - IM(cc[ac+ido])), taui);

                    RE(d2) = RE(c2) - IM(c3);
                    IM(d3) = IM(c2) - RE(c3);
                    RE(d3) = RE(c2) + IM(c3);
                    IM(d2) = IM(c2) + RE(c3);

#if 1
                    ComplexMult(&IM(ch[ah+l1*ido]), &RE(ch[ah+l1*ido]),
                        IM(d2), RE(d2), RE(wa1[i]), IM(wa1[i]));
                    ComplexMult(&IM(ch[ah+2*l1*ido]), &RE(ch[ah+2*l1*ido]),
                        IM(d3), RE(d3), RE(wa2[i]), IM(wa2[i]));
#else
                    ComplexMult(&RE(ch[ah+l1*ido]), &IM(ch[ah+l1*ido]),
                        RE(d2), IM(d2), RE(wa1[i]), IM(wa1[i]));
                    ComplexMult(&RE(ch[ah+2*l1*ido]), &IM(ch[ah+2*l1*ido]),
                        RE(d3), IM(d3), RE(wa2[i]), IM(wa2[i]));
#endif
                }
            }
        } else {
            for (k = 0; k < l1; k++)
            {
                for (i = 0; i < ido; i++)
                {
                    ac = i + (3*k+1)*ido;
                    ah = i + k * ido;

                    RE(t2) = RE(cc[ac]) + RE(cc[ac+ido]);
                    RE(c2) = RE(cc[ac-ido]) + MUL_F(RE(t2),taur);
                    IM(t2) = IM(cc[ac]) + IM(cc[ac+ido]);
                    IM(c2) = IM(cc[ac-ido]) + MUL_F(IM(t2),taur);

                    RE(ch[ah]) = RE(cc[ac-ido]) + RE(t2);
                    IM(ch[ah]) = IM(cc[ac-ido]) + IM(t2);

                    RE(c3) = MUL_F((RE(cc[ac]) - RE(cc[ac+ido])), taui);
                    IM(c3) = MUL_F((IM(cc[ac]) - IM(cc[ac+ido])), taui);

                    RE(d2) = RE(c2) + IM(c3);
                    IM(d3) = IM(c2) + RE(c3);
                    RE(d3) = RE(c2) - IM(c3);
                    IM(d2) = IM(c2) - RE(c3);

#if 1
                    ComplexMult(&RE(ch[ah+l1*ido]), &IM(ch[ah+l1*ido]),
                        RE(d2), IM(d2), RE(wa1[i]), IM(wa1[i]));
                    ComplexMult(&RE(ch[ah+2*l1*ido]), &IM(ch[ah+2*l1*ido]),
                        RE(d3), IM(d3), RE(wa2[i]), IM(wa2[i]));
#else
                    ComplexMult(&IM(ch[ah+l1*ido]), &RE(ch[ah+l1*ido]),
                        IM(d2), RE(d2), RE(wa1[i]), IM(wa1[i]));
                    ComplexMult(&IM(ch[ah+2*l1*ido]), &RE(ch[ah+2*l1*ido]),
                        IM(d3), RE(d3), RE(wa2[i]), IM(wa2[i]));
#endif
                }
            }
        }
    }
}

static void RENAME(passf4pos)(const uint16_t ido, const uint16_t l1, const complex_t *cc,
                      complex_t *ch, const complex_t *wa1, const complex_t *wa2,
                      const complex_t *wa3)
{
    uint16_t i, k, ac, ah;

    if (ido == 1)
    {
        for (k = 0; k < l1; k++)
        {
            complex_t t1, t2, t3, t4;

            ac = 4*k;
            ah = k;

            RE(t2) = RE(cc[ac])   + RE(cc[ac+2]);
            RE(t1) = RE(cc[ac])   - RE(cc[ac+2]);
            IM(t2) = IM(cc[ac])   + IM(cc[ac+2]);
            IM(t1) = IM(cc[ac])   - IM(cc[ac+2]);
            RE(t3) = RE(cc[ac+1]) + RE(cc[ac+3]);
            IM(t4) = RE(cc[ac+1]) - RE(cc[ac+3]);
            IM(t3) = IM(cc[ac+3]) + IM(cc[ac+1]);
            RE(t4) = IM(cc[ac+3]) - IM(cc[ac+1]);

            RE(ch[ah])      = RE(t2) + RE(t3);
            RE(ch[ah+2*l1]) = RE(t2) - RE(t3);

            IM(ch[ah])      = IM(t2) + IM(t3);
            IM(ch[ah+2*l1]) = IM(t2) - IM(t3);

            RE(ch[ah+l1])   = RE(t1) + RE(t4);
            RE(ch[ah+3*l1]) = RE(t1) - RE(t4);

            IM(ch[ah+l1])   = IM(t1) + IM(t4);
            IM(ch[ah+3*l1]) = IM(t1) - IM(t4);
        }
    } else {
        for (k = 0; k < l1; k++)
        {
            ac = 4*k*ido;
            ah = k*ido;

            for (i = 0; i < ido; i++)
            {
                complex_t c2, c3, c4, t1, t2, t3, t4;

                RE(t2) = RE(cc[ac+i]) + RE(cc[ac+i+2*ido]);
                RE(t1) = RE(cc[ac+i]) - RE(cc[ac+i+2*ido]);
                IM(t2) = IM(cc[ac+i]) + IM(cc[ac+i+2*ido]);
                IM(t1) = IM(cc[ac+i]) - IM(cc[ac+i+2*ido]);
                RE(t3) = RE(cc[ac+i+ido]) + RE(cc[ac+i+3*ido]);
                IM(t4) = RE(cc[ac+i+ido]) - RE(cc[ac+i+3*ido]);
                IM(t3) = IM(cc[ac+i+3*ido]) + IM(cc[ac+i+ido]);
                RE(t4) = IM(cc[ac+i+3*ido]) - IM(cc[ac+i+ido]);

                RE(c2) = RE(t1) + RE(t4);
                RE(c4) = RE(t1) - RE(t4);

                IM(c2) = IM(t1) + IM(t4);
                IM(c4) = IM(t1) - IM(t4);

                RE(ch[ah+i]) = RE(t2) + RE(t3);
                RE(c3)       = RE(t2) - RE(t3);

                IM(ch[ah+i]) = IM(t2) + IM(t3);
                IM(c3)       = IM(t2) - IM(t3);

#if 1
                ComplexMult(&IM(ch[ah+i+l1*ido]), &RE(ch[ah+i+l1*ido]),
                    IM(c2), RE(c2), RE(wa1[i]), IM(wa1[i]));
                ComplexMult(&IM(ch[ah+i+2*l1*ido]), &RE(ch[ah+i+2*l1*ido]),
                    IM(c3), RE(c3), RE(wa2[i]), IM(wa2[i]));
                ComplexMult(&IM(ch[ah+i+3*l1*ido]), &RE(ch[ah+i+3*l1*ido]),
                    IM(c4), RE(c4), RE(wa3[i]), IM(wa3[i]));
#else
                ComplexMult(&RE(ch[ah+i+l1*ido]), &IM(ch[ah+i+l1*ido]),
                    RE(c2), IM(c2), RE(wa1[i]), IM(wa1[i]));
                ComplexMult(&RE(ch[ah+i+2*l1*ido]), &IM(ch[ah+i+2*l1*ido]),
                    RE(c3), IM(c3), RE(wa2[i]), IM(wa2[i]));
                ComplexMult(&RE(ch[ah+i+3*l1*ido]), &IM(ch[ah+i+3*l1*ido]),
                    RE(c4), IM(c4), RE(wa3[i]), IM(wa3[i]));
#endif
            }
        }
    }
}

static void RENAME(passf4neg)(const uint16_t ido, const uint16_t l1, const complex_t *cc,
                      complex_t *ch, const complex_t *wa1, const complex_t *wa2,
                      const complex_t *wa3)
{
    uint16_t i, k, ac, ah;

    if (ido == 1)
    {
        for (k = 0; k < l1; k++)
        {
            complex_t t1, t2, t3, t4;

            ac = 4*k;
            ah = k;

            RE(t2) = RE(cc[ac])   + RE(cc[ac+2]);
            RE(t1) = RE(cc[ac])   - RE(cc[ac+2]);
            IM(t2) = IM(cc[ac])   + IM(cc[ac+2]);
            IM(t1) = IM(cc[ac])   - IM(cc[ac+2]);
            RE(t3) = RE(cc[ac+1]) + RE(cc[ac+3]);
            IM(t4) = RE(cc[ac+1]) - RE(cc[ac+3]);
            IM(t3) = IM(cc[ac+3]) + IM(cc[ac+1]);
            RE(t4) = IM(cc[ac+3]) - IM(cc[ac+1]);

            RE(ch[ah])      = RE(t2) + RE(t3);
            RE(ch[ah+2*l1]) = RE(t2) - RE(t3);

            IM(ch[ah])      = IM(t2) + IM(t3);
            IM(ch[ah+2*l1]) = IM(t2) - IM(t3);

            RE(ch[ah+l1])   = RE(t1) - RE(t4);
            RE(ch[ah+3*l1]) = RE(t1) + RE(t4);

            IM(ch[ah+l1])   = IM(t1) - IM(t4);
            IM(ch[ah+3*l1]) = IM(t1) + IM(t4);
        }
    } else {
        for (k = 0; k < l1; k++)
        {
            ac = 4*k*ido;
            ah = k*ido;

            for (i = 0; i < ido; i++)
            {
                complex_t c2, c3, c4, t1, t2, t3, t4;

                RE(t2) = RE(cc[ac+i]) + RE(cc[ac+i+2*ido]);
                RE(t1) = RE(cc[ac+i]) - RE(cc[ac+i+2*ido]);
                IM(t2) = IM(cc[ac+i]) + IM(cc[ac+i+2*ido]);
                IM(t1) = IM(cc[ac+i]) - IM(cc[ac+i+2*ido]);
                RE(t3) = RE(cc[ac+i+ido]) + RE(cc[ac+i+3*ido]);
                IM(t4) = RE(cc[ac+i+ido]) - RE(cc[ac+i+3*ido]);
                IM(t3) = IM(cc[ac+i+3*ido]) + IM(cc[ac+i+ido]);
                RE(t4) = IM(cc[ac+i+3*ido]) - IM(cc[ac+i+ido]);

                RE(c2) = RE(t1) - RE(t4);
                RE(c4) = RE(t1) + RE(t4);

                IM(c2) = IM(t1) - IM(t4);
                IM(c4) = IM(t1) + IM(t4);

                RE(ch[ah+i]) = RE(t2) + RE(t3);
                RE(c3)       = RE(t2) - RE(t3);

                IM(ch[ah+i]) = IM(t2) + IM(t3);
                IM(c3)       = IM(t2) - IM(t3);

#if 1
                ComplexMult(&RE(ch[ah+i+l1*ido]), &IM(ch[ah+i+l1*ido]),
                    RE(c2), IM(c2), RE(wa1[i]), IM(wa1[i]));
                ComplexMult(&RE(ch[ah+i+2*l1*ido]), &IM(ch[ah+i+2*l1*ido]),
                    RE(c3), IM(c3), RE(wa2[i]), IM(wa2[i]));
                ComplexMult(&RE(ch[ah+i+3*l1*ido]), &IM(ch[ah+i+3*l1*ido]),
                    RE(c4), IM(c4), RE(wa3[i]), IM(wa3[i]));
#else
                ComplexMult(&IM(ch[ah+i+l1*ido]), &RE(ch[ah+i+l1*ido]),
                    IM(c2), RE(c2), RE(wa1[i]), IM(wa1[i]));
                ComplexMult(&IM(ch[ah+i+2*l1*ido]), &RE(ch[ah+i+2*l1*ido]),
                    IM(c3), RE(c3), RE(wa2[i]), IM(wa2[i]));
                ComplexMult(&IM(ch[ah+i+3*l1*ido]), &RE(ch[ah+i+3*l1*ido]),
                    IM(c4), RE(c4), RE(wa3[i]), IM(wa3[i]));
#endif
            }
        }
    }
}

static void RENAME(passf5)(const uint16_t ido, const uint16_t l1, const complex_t *cc,
                   complex_t *ch, const complex_t *wa1, const complex_t *wa2, const complex_t *wa3,
                   const complex_t *wa4, const int8_t isign)
{
    static real_t tr11 = FRAC_CONST(0.309016994374947);
    static real_t ti11 = FRAC_CONST(0.951056516295154);
    static real_t tr12 = FRAC_CONST(-0.809016994374947);
    static real_t ti12 = FRAC_CONST(0.587785252292473);
    uint16_t i, k, ac, ah;
    complex_t c2, c3, c4, c5, d3, d4, d5, d2, t2, t3, t4, t5;

    if (ido == 1)
    {
        if (isign == 1)
        {
            for (k = 0; k < l1; k++)
            {
                ac = 5*k + 1;
                ah = k;

                RE(t2) = RE(cc[ac]) + RE(cc[ac+3]);
                IM(t2) = IM(cc[ac]) + IM(cc[ac+3]);
                RE(t3) = RE(cc[ac+1]) + RE(cc[ac+2]);
                IM(t3) = IM(cc[ac+1]) + IM(cc[ac+2]);
                RE(t4) = RE(cc[ac+1]) - RE(cc[ac+2]);
                IM(t4) = IM(cc[ac+1]) - IM(cc[ac+2]);
                RE(t5) = RE(cc[ac]) - RE(cc[ac+3]);
                IM(t5) = IM(cc[ac]) - IM(cc[ac+3]);

                RE(ch[ah]) = RE(cc[ac-1]) + RE(t2) + RE(t3);
                IM(ch[ah]) = IM(cc[ac-1]) + IM(t2) + IM(t3);

                RE(c2) = RE(cc[ac-1]) + MUL_F(RE(t2),tr11) + MUL_F(RE(t3),tr12);
                IM(c2) = IM(cc[ac-1]) + MUL_F(IM(t2),tr11) + MUL_F(IM(t3),tr12);
                RE(c3) = RE(cc[ac-1]) + MUL_F(RE(t2),tr12) + MUL_F(RE(t3),tr11);
                IM(c3) = IM(cc[ac-1]) + MUL_F(IM(t2),tr12) + MUL_F(IM(t3),tr11);

                ComplexMult(&RE(c5), &RE(c4),
                    ti11, ti12, RE(t5), RE(t4));
                ComplexMult(&IM(c5), &IM(c4),
                    ti11, ti12, IM(t5), IM(t4));

                RE(ch[ah+l1]) = RE(c2) - IM(c5);
                IM(ch[ah+l1]) = IM(c2) + RE(c5);
                RE(ch[ah+2*l1]) = RE(c3) - IM(c4);
                IM(ch[ah+2*l1]) = IM(c3) + RE(c4);
                RE(ch[ah+3*l1]) = RE(c3) + IM(c4);
                IM(ch[ah+3*l1]) = IM(c3) - RE(c4);
                RE(ch[ah+4*l1]) = RE(c2) + IM(c5);
                IM(ch[ah+4*l1]) = IM(c2) - RE(c5);
            }
        } else {
            for (k = 0; k < l1; k++)
            {
                ac = 5*k + 1;
                ah = k;

                RE(t2) = RE(cc[ac]) + RE(cc[ac+3]);
                IM(t2) = IM(cc[ac]) + IM(cc[ac+3]);
                RE(t3) = RE(cc[ac+1]) + RE(cc[ac+2]);
                IM(t3) = IM(cc[ac+1]) + IM(cc[ac+2]);
                RE(t4) = RE(cc[ac+1]) - RE(cc[ac+2]);
                IM(t4) = IM(cc[ac+1]) - IM(cc[ac+2]);
                RE(t5) = RE(cc[ac]) - RE(cc[ac+3]);
                IM(t5) = IM(cc[ac]) - IM(cc[ac+3]);

                RE(ch[ah]) = RE(cc[ac-1]) + RE(t2) + RE(t3);
                IM(ch[ah]) = IM(cc[ac-1]) + IM(t2) + IM(t3);

                RE(c2) = RE(cc[ac-1]) + MUL_F(RE(t2),tr11) + MUL_F(RE(t3),tr12);
                IM(c2) = IM(cc[ac-1]) + MUL_F(IM(t2),tr11) + MUL_F(IM(t3),tr12);
                RE(c3) = RE(cc[ac-1]) + MUL_F(RE(t2),tr12) + MUL_F(RE(t3),tr11);
                IM(c3) = IM(cc[ac-1]) + MUL_F(IM(t2),tr12) + MUL_F(IM(t3),tr11);

                ComplexMult(&RE(c4), &RE(c5),
                    ti12, ti11, RE(t5), RE(t4));
                ComplexMult(&IM(c4), &IM(c5),
                    ti12, ti12, IM(t5), IM(t4));

                RE(ch[ah+l1]) = RE(c2) + IM(c5);
                IM(ch[ah+l1]) = IM(c2) - RE(c5);
                RE(ch[ah+2*l1]) = RE(c3) + IM(c4);
                IM(ch[ah+2*l1]) = IM(c3) - RE(c4);
                RE(ch[ah+3*l1]) = RE(c3) - IM(c4);
                IM(ch[ah+3*l1]) = IM(c3) + RE(c4);
                RE(ch[ah+4*l1]) = RE(c2) - IM(c5);
                IM(ch[ah+4*l1]) = IM(c2) + RE(c5);
            }
        }
    } else {
        if (isign == 1)
        {
            for (k = 0; k < l1; k++)
            {
                for (i = 0; i < ido; i++)
                {
                    ac = i + (k*5 + 1) * ido;
                    ah = i + k * ido;

                    RE(t2) = RE(cc[ac]) + RE(cc[ac+3*ido]);
                    IM(t2) = IM(cc[ac]) + IM(cc[ac+3*ido]);
                    RE(t3) = RE(cc[ac+ido]) + RE(cc[ac+2*ido]);
                    IM(t3) = IM(cc[ac+ido]) + IM(cc[ac+2*ido]);
                    RE(t4) = RE(cc[ac+ido]) - RE(cc[ac+2*ido]);
                    IM(t4) = IM(cc[ac+ido]) - IM(cc[ac+2*ido]);
                    RE(t5) = RE(cc[ac]) - RE(cc[ac+3*ido]);
                    IM(t5) = IM(cc[ac]) - IM(cc[ac+3*ido]);

                    RE(ch[ah]) = RE(cc[ac-ido]) + RE(t2) + RE(t3);
                    IM(ch[ah]) = IM(cc[ac-ido]) + IM(t2) + IM(t3);

                    RE(c2) = RE(cc[ac-ido]) + MUL_F(RE(t2),tr11) + MUL_F(RE(t3),tr12);
                    IM(c2) = IM(cc[ac-ido]) + MUL_F(IM(t2),tr11) + MUL_F(IM(t3),tr12);
                    RE(c3) = RE(cc[ac-ido]) + MUL_F(RE(t2),tr12) + MUL_F(RE(t3),tr11);
                    IM(c3) = IM(cc[ac-ido]) + MUL_F(IM(t2),tr12) + MUL_F(IM(t3),tr11);

                    ComplexMult(&RE(c5), &RE(c4),
                        ti11, ti12, RE(t5), RE(t4));
                    ComplexMult(&IM(c5), &IM(c4),
                        ti11, ti12, IM(t5), IM(t4));

                    IM(d2) = IM(c2) + RE(c5);
                    IM(d3) = IM(c3) + RE(c4);
                    RE(d4) = RE(c3) + IM(c4);
                    RE(d5) = RE(c2) + IM(c5);
                    RE(d2) = RE(c2) - IM(c5);
                    IM(d5) = IM(c2) - RE(c5);
                    RE(d3) = RE(c3) - IM(c4);
                    IM(d4) = IM(c3) - RE(c4);

#if 1
                    ComplexMult(&IM(ch[ah+l1*ido]), &RE(ch[ah+l1*ido]),
                        IM(d2), RE(d2), RE(wa1[i]), IM(wa1[i]));
                    ComplexMult(&IM(ch[ah+2*l1*ido]), &RE(ch[ah+2*l1*ido]),
                        IM(d3), RE(d3), RE(wa2[i]), IM(wa2[i]));
                    ComplexMult(&IM(ch[ah+3*l1*ido]), &RE(ch[ah+3*l1*ido]),
                        IM(d4), RE(d4), RE(wa3[i]), IM(wa3[i]));
                    ComplexMult(&IM(ch[ah+4*l1*ido]), &RE(ch[ah+4*l1*ido]),
                        IM(d5), RE(d5), RE(wa4[i]), IM(wa4[i]));
#else
                    ComplexMult(&RE(ch[ah+l1*ido]), &IM(ch[ah+l1*ido]),
                        RE(d2), IM(d2), RE(wa1[i]), IM(wa1[i]));
                    ComplexMult(&RE(ch[ah+2*l1*ido]), &IM(ch[ah+2*l1*ido]),
                        RE(d3), IM(d3), RE(wa2[i]), IM(wa2[i]));
                    ComplexMult(&RE(ch[ah+3*l1*ido]), &IM(ch[ah+3*l1*ido]),
                        RE(d4), IM(d4), RE(wa3[i]), IM(wa3[i]));
                    ComplexMult(&RE(ch[ah+4*l1*ido]), &IM(ch[ah+4*l1*ido]),
                        RE(d5), IM(d5), RE(wa4[i]), IM(wa4[i]));
#endif
                }
            }
        } else {
            for (k = 0; k < l1; k++)
            {
                for (i = 0; i < ido; i++)
                {
                    ac = i + (k*5 + 1) * ido;
                    ah = i + k * ido;

                    RE(t2) = RE(cc[ac]) + RE(cc[ac+3*ido]);
                    IM(t2) = IM(cc[ac]) + IM(cc[ac+3*ido]);
                    RE(t3) = RE(cc[ac+ido]) + RE(cc[ac+2*ido]);
                    IM(t3) = IM(cc[ac+ido]) + IM(cc[ac+2*ido]);
                    RE(t4) = RE(cc[ac+ido]) - RE(cc[ac+2*ido]);
                    IM(t4) = IM(cc[ac+ido]) - IM(cc[ac+2*ido]);
                    RE(t5) = RE(cc[ac]) - RE(cc[ac+3*ido]);
                    IM(t5) = IM(cc[ac]) - IM(cc[ac+3*ido]);

                    RE(ch[ah]) = RE(cc[ac-ido]) + RE(t2) + RE(t3);
                    IM(ch[ah]) = IM(cc[ac-ido]) + IM(t2) + IM(t3);

                    RE(c2) = RE(cc[ac-ido]) + MUL_F(RE(t2),tr11) + MUL_F(RE(t3),tr12);
                    IM(c2) = IM(cc[ac-ido]) + MUL_F(IM(t2),tr11) + MUL_F(IM(t3),tr12);
                    RE(c3) = RE(cc[ac-ido]) + MUL_F(RE(t2),tr12) + MUL_F(RE(t3),tr11);
                    IM(c3) = IM(cc[ac-ido]) + MUL_F(IM(t2),tr12) + MUL_F(IM(t3),tr11);

                    ComplexMult(&RE(c4), &RE(c5),
                        ti12, ti11, RE(t5), RE(t4));
                    ComplexMult(&IM(c4), &IM(c5),
                        ti12, ti12, IM(t5), IM(t4));

                    IM(d2) = IM(c2) - RE(c5);
                    IM(d3) = IM(c3) - RE(c4);
                    RE(d4) = RE(c3) - IM(c4);
                    RE(d5) = RE(c2) - IM(c5);
                    RE(d2) = RE(c2) + IM(c5);
                    IM(d5) = IM(c2) + RE(c5);
                    RE(d3) = RE(c3) + IM(c4);
                    IM(d4) = IM(c3) + RE(c4);

#if 1
                    ComplexMult(&RE(ch[ah+l1*ido]), &IM(ch[ah+l1*ido]),
                        RE(d2), IM(d2), RE(wa1[i]), IM(wa1[i]));
                    ComplexMult(&RE(ch[ah+2*l1*ido]), &IM(ch[ah+2*l1*ido]),
                        RE(d3), IM(d3), RE(wa2[i]), IM(wa2[i]));
                    ComplexMult(&RE(ch[ah+3*l1*ido]), &IM(ch[ah+3*l1*ido]),
                        RE(d4), IM(d4), RE(wa3[i]), IM(wa3[i]));
                    ComplexMult(&RE(ch[ah+4*l1*ido]), &IM(ch[ah+4*l1*ido]),
                        RE(d5), IM(d5), RE(wa4[i]), IM(wa4[i]));
#else
                    ComplexMult(&IM(ch[ah+l1*ido]), &RE(ch[ah+l1*ido]),
                        IM(d2), RE(d2), RE(wa1[i]), IM(wa1[i]));
                    ComplexMult(&IM(ch[ah+2*l1*ido]), &RE(ch[ah+2*l1*ido]),
                        IM(d3), RE(d3), RE(wa2[i]), IM(wa2[i]));
                    ComplexMult(&IM(ch[ah+3*l1*ido]), &RE(ch[ah+3*l1*ido]),
                        IM(d4), RE(d4), RE(wa3[i]), IM(wa3[i]));
                    ComplexMult(&IM(ch[ah+4*l1*ido]), &RE(ch[ah+4*l1*ido]),
                        IM(d5), RE(d5), RE(wa4[i]), IM(wa4[i]));
#endif
                }
            }
        }
    }
}

static void RENAME(cfftf1pos)(uint16_t n, complex_t *c, complex_t *ch,
                             const uint16_t *ifac, const complex_t *wa,
                             const int8_t isign)
{
    uint16_t i;
    uint16_t k1, l1, l2;
    uint16_t na, nf, ip, iw, ix2, ix3, ix4, ido, idl1;

    nf = ifac[1];
    na = 0;
    l1 = 1;
    iw = 0;

    for (k1 = 2; k1 <= nf+1; k1++)
    {
        ip = ifac[k1];
        l2 = ip*l1;
        ido = n / l2;
        idl1 = ido*l1;

        switch (ip)
        {
        case 4:
            ix2 = iw + ido;
            ix3 = ix2 + ido;

            if (na == 0)
                passf4pos((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)c, ch, &wa[iw], &wa[ix2], &wa[ix3]);
            else
                passf4pos((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)ch, c, &wa[iw], &wa[ix2], &wa[ix3]);

            na = 1 - na;
            break;
        case 2:
            if (na == 0)
                passf2pos((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)c, ch, &wa[iw]);
            else
                passf2pos((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)ch, c, &wa[iw]);

            na = 1 - na;
            break;
        case 3:
            ix2 = iw + ido;

            if (na == 0)
                passf3((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)c, ch, &wa[iw], &wa[ix2], isign);
            else
                passf3((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)ch, c, &wa[iw], &wa[ix2], isign);

            na = 1 - na;
            break;
        case 5:
            ix2 = iw + ido;
            ix3 = ix2 + ido;
            ix4 = ix3 + ido;

            if (na == 0)
                passf5((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)c, ch, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4], isign);
            else
                passf5((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)ch, c, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4], isign);

            na = 1 - na;
            break;
        }

        l1 = l2;
        iw += (ip-1) * ido;
    }

    if (na == 0)
        return;

    for (i = 0; i < n; i++)
    {
        RE(c[i]) = RE(ch[i]);
        IM(c[i]) = IM(ch[i]);
    }
}

static void RENAME(cfftf1neg)(uint16_t n, complex_t *c, complex_t *ch,
                             const uint16_t *ifac, const complex_t *wa,
                             const int8_t isign)
{
    uint16_t i;
    uint16_t k1, l1, l2;
    uint16_t na, nf, ip, iw, ix2, ix3, ix4, ido, idl1;

    nf = ifac[1];
    na = 0;
    l1 = 1;
    iw = 0;

    for (k1 = 2; k1 <= nf+1; k1++)
    {
        ip = ifac[k1];
        l2 = ip*l1;
        ido = n / l2;
        idl1 = ido*l1;

        switch (ip)
        {
        case 4:
            ix2 = iw + ido;
            ix3 = ix2 + ido;

            if (na == 0)
                passf4neg((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)c, ch, &wa[iw], &wa[ix2], &wa[ix3]);
            else
                passf4neg((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)ch, c, &wa[iw], &wa[ix2], &wa[ix3]);

            na = 1 - na;
            break;
        case 2:
            if (na == 0)
                passf2neg((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)c, ch, &wa[iw]);
            else
                passf2neg((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)ch, c, &wa[iw]);

            na = 1 - na;
            break;
        case 3:
            ix2 = iw + ido;

            if (na == 0)
                passf3((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)c, ch, &wa[iw], &wa[ix2], isign);
            else
                passf3((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)ch, c, &wa[iw], &wa[ix2], isign);

            na = 1 - na;
            break;
        case 5:
            ix2 = iw + ido;
            ix3 = ix2 + ido;
            ix4 = ix3 + ido;

            if (na == 0)
                passf5((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)c, ch, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4], isign);
            else
                passf5((const uint16_t)ido, (const uint16_t)l1, (const complex_t*)ch, c, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4], isign);

            na = 1 - na;
            break;
        }

        l1 = l2;
        iw += (ip-1) * ido;
    }

    if (na == 0)
        return;

    for (i = 0; i < n; i++)
    {
        RE(c[i]) = RE(ch[i]);
        IM(c[i]) = IM(ch[i]);
    }
}
