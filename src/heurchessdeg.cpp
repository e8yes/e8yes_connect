#include <iostream>
#include <string>
#include <vector>
#include <utility>
#include "state.h"
#include "heurchessdeg.h"

typedef std::pair<unsigned, unsigned> chess_pos_t;


struct EvalFRData
{
        EvalFRData(int who, unsigned l, unsigned r, unsigned k):
                who(who), k(k), l(l), r(r)
        {
        }

        int             who;
        unsigned        k;
        unsigned        l;
        unsigned        r;
        float           score = 0.0f;
};

static bool eval_fr(const char* val, int x, int y, unsigned dist, void* data)
{
        EvalFRData* fr = (EvalFRData*) data;

        if (*val == State::NO_PIECE) {
                fr->score += 1.0f/fr->k;
                return true;
        } else if (*val == fr->who) {
                // Space or our chess.
                fr->score += fr->l/(float) dist;
                return false;
        } else {
                // Opponent.
                return false;
        }
}

// Tells me the effective degrees of freedom in direction d.
static float fr(const State& s, int x, int y, int who, unsigned d, int l, int k)
{
 //     unsigned r = std::max(0, k - l);
        unsigned r = k - l;
//      float adv_freedom = k/(float) r;

        EvalFRData fr(who, l, r, k);
        s.scan(x, y, d, ::eval_fr, &fr);
        return fr.score;
}

static bool eval_s(const char* val, int x, int y, unsigned dist, void* data)
{
        return *val == *(int*) data;
}

// Tells me the length of the connect in direction d.
static unsigned s(const State& s, int x, int y, int who, unsigned d)
{
        return s.scan(x, y, d, ::eval_s, &who);
}

static float eval_xy(const State& s, int x, int y, int who)
{
        float score = 0;
        for (unsigned d = 0; d < 8; d ++) {
                unsigned l = ::s(s, x, y, who, d);
                float alpha = ::fr(s, x, y, who, (d + 4)%8, l, s.k);
                score += alpha*l;
        }
        return score;
}

static bool eval_cr(const char* val, int x, int y, unsigned dist, void* data)
{
        return dist == 0 || *val == State::NO_PIECE || *val == *(int*) data;
}

// Tells me the effective degrees of freedom in direction d.
static float cr(const State& s, int x, int y, int who, unsigned d, int l, int k)
{
        float dist = (float) s.scan(x, y, d, ::eval_cr, &who);
        return 1.0f/(dist*dist)*l*l;
}

static float eval_xy_oppo(const State& s, int x, int y, int who)
{
        float score = 0;
        for (unsigned d = 0; d < 8; d ++) {
                unsigned l = ::s(s, x, y, who, d);
                score += ::cr(s, x, y, who, (d + 4)%8, l, s.k);
        }
        return score;
}

struct EvalAffectedData
{
        EvalAffectedData(std::vector<chess_pos_t>& ai_chesses,
                         std::vector<chess_pos_t>& oppo_chesses):
                ai_chesses(ai_chesses),
                oppo_chesses(oppo_chesses)
        {
        }

        std::vector<chess_pos_t>&       ai_chesses;
        std::vector<chess_pos_t>&       oppo_chesses;
};

static bool eval_affected(const char* val, int x, int y, unsigned dist, void* data)
{
        EvalAffectedData* af_data = (EvalAffectedData*) data;

        switch (*val) {
                case State::NO_PIECE:
                        return true;
                case State::AI_PIECE:
                        af_data->ai_chesses.push_back(chess_pos_t(x, y));
                        return false;
                case State::HUMAN_PIECE:
                        af_data->oppo_chesses.push_back(chess_pos_t(x, y));
                        return false;
        }
        return true;
}

static void find_affected_chess(const State& s, const Move& move,
                                std::vector<chess_pos_t>& ai_chesses, std::vector<chess_pos_t>& oppo_chesses)
{
        EvalAffectedData data(ai_chesses, oppo_chesses);
        for (unsigned d = 0; d < 8; d ++) {
                s.scan(move.x, move.y, d, ::eval_affected, &data);
        }
}

static float full_board_eval_for(const State& s, int who)
{
        float score = 0;
        for (unsigned y = 0; y < s.num_rows; y ++) {
                for (unsigned x = 0; x < s.num_cols; x ++) {
                        score += ::eval_xy(s, x, y, who);
                }
        }
        return score;
}

static float full_board_eval(const State& s, const Move& next_move)
{
        float new_ai_score = full_board_eval_for(s, State::AI_PIECE);
        float new_oppo_score = full_board_eval_for(s, State::HUMAN_PIECE);

        float p0 = new_ai_score;
        float p1 = new_oppo_score;

        return p0 - p1;
}

static float incremental_eval(const State& k, const Move& next_move)
{
        // Faking a const operation.
        State& s = const_cast<State&>(k);

        std::vector<chess_pos_t> affected_ai, affected_oppo;
        ::find_affected_chess(s, next_move, affected_ai, affected_oppo);

        int n_ai = affected_ai.size();
        int n_oppo = affected_oppo.size();

        float new_ai_score = 0;
        for (unsigned i = 0; i < affected_ai.size(); i ++) {
                new_ai_score += ::eval_xy(s, affected_ai[i].first, affected_ai[i].second, State::AI_PIECE);
        }
        float new_oppo_score = 0;
        for (unsigned i = 0; i < affected_oppo.size(); i ++) {
                new_oppo_score += ::eval_xy(s, affected_oppo[i].first, affected_oppo[i].second, State::HUMAN_PIECE);
        }

        if (s.is(next_move.x, next_move.y) == State::AI_PIECE) {
                new_ai_score += ::eval_xy(s, next_move.x, next_move.y, State::AI_PIECE);
                n_ai ++;
        } else {
                new_oppo_score += ::eval_xy(s, next_move.x, next_move.y, State::HUMAN_PIECE);
                n_oppo ++;
        }

        n_ai = std::max(1, n_ai);
        n_oppo = std::max(1, n_oppo);

        float p0 = new_ai_score/n_ai;
        float p1 = new_oppo_score/n_oppo;

        return p0 - p1;
}

// Public API.
float HeuristicChessDegree::evaluate(const State& k, const Move& next_move) const
{
        return ::incremental_eval(k, next_move);
        //return ::full_board_eval(k, next_move);
}
