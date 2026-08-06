/*******************************************************************************
 * RISC-V Opcode Classifier
 *
 * OpcodeClassifier provides one predicate per implemented ISA extension:
 *
 *     bool opcode_is_<ext>(uint32_t opcode)
 *
 * Each predicate returns true iff the given opcode is a valid encoding of
 * an instruction belonging to that extension. Classification delegates to
 * the extension decoders, so it stays consistent with the model's
 * decoder strictness (invalid encodings classify as false everywhere).
 *
 * Notes:
 *  - Compressed extensions (C, Zcf a.k.a. "FC") use only the low 16 bits
 *    of the opcode value; the predicate first checks the 16-bit encoding
 *    space marker (bits [1:0] != 0b11).
 *  - RV32I here includes the privileged instructions the base decoder
 *    handles (MRET, WFI; SRET/SFENCE.VMA when S-mode is enabled).
 *  - Zicsr is restricted to the CSR funct3 encodings (1-3, 5-7) of the
 *    SYSTEM opcode; ECALL/EBREAK/MRET/... classify as RV32I.
 ******************************************************************************/

#ifndef RISCV_OPCODE_CLASSIFY_HPP
#define RISCV_OPCODE_CLASSIFY_HPP

#include "riscv_common.hpp"
#include "riscv_rv32i.hpp"
#include "riscv_rv32m.hpp"
#include "riscv_rv32a.hpp"
#include "riscv_rv32c.hpp"
#include "riscv_rv32f.hpp"
#include "riscv_rv32fc.hpp"
#include "riscv_zicsr.hpp"
#include "riscv_zifencei.hpp"
#include "riscv_zba.hpp"
#include "riscv_zbb.hpp"
#include "riscv_zbs.hpp"
#include "riscv_zicond.hpp"

namespace riscv {

class OpcodeClassifier {
public:
    // Decode SRET/SFENCE.VMA as part of the base ISA only when S-mode
    // is implemented (mirrors rv32i::Decoder::s_mode_enabled).
    explicit OpcodeClassifier(bool s_mode_enabled = false) {
        rv32i_dec_.s_mode_enabled = s_mode_enabled;
    }

    // ---- Base ISA -----------------------------------------------------

    bool opcode_is_rv32i(uint32_t opcode) const {
        if (is_compressed(opcode)) return false;
        return rv32i_dec_.decode(opcode).type != rv32i::InstrType::ILLEGAL;
    }

    // ---- Standard extensions (32-bit encodings) -----------------------

    bool opcode_is_rv32m(uint32_t opcode) const {
        if (is_compressed(opcode)) return false;
        return rv32m_dec_.decode(opcode).type != rv32m::InstrType::ILLEGAL;
    }

    bool opcode_is_rv32a(uint32_t opcode) const {
        if (is_compressed(opcode)) return false;
        return rv32a_dec_.decode(opcode).type != rv32a::InstrType::ILLEGAL;
    }

    bool opcode_is_rv32f(uint32_t opcode) const {
        if (is_compressed(opcode)) return false;
        return rv32f_dec_.decode(opcode).type != rv32f::InstrType::ILLEGAL;
    }

    bool opcode_is_zicsr(uint32_t opcode) const {
        if (is_compressed(opcode)) return false;
        return zicsr_dec_.decode(opcode).type != zicsr::CSRInstrType::ILLEGAL;
    }

    bool opcode_is_zifencei(uint32_t opcode) const {
        if (is_compressed(opcode)) return false;
        return zifencei_dec_.decode(opcode).type != zifencei::InstrType::ILLEGAL;
    }

    bool opcode_is_zba(uint32_t opcode) const {
        if (is_compressed(opcode)) return false;
        return zba_dec_.decode(opcode).type != zba::InstrType::ILLEGAL;
    }

    bool opcode_is_zbb(uint32_t opcode) const {
        if (is_compressed(opcode)) return false;
        return zbb_dec_.decode(opcode).type != zbb::InstrType::ILLEGAL;
    }

    bool opcode_is_zbs(uint32_t opcode) const {
        if (is_compressed(opcode)) return false;
        return zbs_dec_.decode(opcode).type != zbs::InstrType::ILLEGAL;
    }

    bool opcode_is_zicond(uint32_t opcode) const {
        if (is_compressed(opcode)) return false;
        return zicond_dec_.decode(opcode).type != zicond::InstrType::ILLEGAL;
    }

    // ---- Compressed extensions (16-bit encodings) ---------------------

    bool opcode_is_rv32c(uint32_t opcode) const {
        if (!is_compressed(opcode)) return false;
        return rv32c_dec_.decode(static_cast<uint16_t>(opcode)).type !=
               rv32c::InstrType::ILLEGAL;
    }

    // Compressed single-precision FP loads/stores (C extension with F).
    bool opcode_is_rv32fc(uint32_t opcode) const {
        if (!is_compressed(opcode)) return false;
        return rv32fc_dec_.decode(static_cast<uint16_t>(opcode)).type !=
               rv32fc::InstrType::ILLEGAL;
    }

private:
    // 16-bit encodings occupy the space where bits [1:0] != 0b11.
    static bool is_compressed(uint32_t opcode) {
        return (opcode & 0x3u) != 0x3u;
    }

    rv32i::Decoder    rv32i_dec_;
    rv32m::Decoder    rv32m_dec_;
    rv32a::Decoder    rv32a_dec_;
    rv32c::Decoder    rv32c_dec_;
    rv32f::Decoder    rv32f_dec_;
    rv32fc::Decoder   rv32fc_dec_;
    zicsr::Decoder    zicsr_dec_;
    zifencei::Decoder zifencei_dec_;
    zba::Decoder      zba_dec_;
    zbb::Decoder      zbb_dec_;
    zbs::Decoder      zbs_dec_;
    zicond::Decoder   zicond_dec_;
};

} // namespace riscv

#endif // RISCV_OPCODE_CLASSIFY_HPP
