struct TriggerDecision {
    bool ok=false;
    std::string reason;
    bool has_cell=false, has_pnp=false;
    cv::Rect cell, pnp;
    float cell_score=0.f, pnp_score=0.f;
};

static TriggerDecision evaluate_trigger_cpp(bool has_cell, const cv::Rect& cell, float cell_score,
                                            bool has_pnp,  const cv::Rect& pnp,  float pnp_score,
                                            float cell_min_score, float pnp_min_score)
{
    TriggerDecision d;
    d.has_cell = has_cell; d.cell = cell; d.cell_score = cell_score;
    d.has_pnp  = has_pnp;  d.pnp  = pnp;  d.pnp_score  = pnp_score;

    if (!has_cell && !has_pnp) { d.ok=false; d.reason="no_cell_no_pnp"; return d; }
    if (!has_cell) { d.ok=false; d.reason="no_cell"; return d; }
    if (!has_pnp)  { d.ok=false; d.reason="no_pnp";  return d; }
    if (cell_score < cell_min_score) { d.ok=false; d.reason="cell_score_low"; return d; }
    if (pnp_score  < pnp_min_score)  { d.ok=false; d.reason="pnp_score_low";  return d; }
    d.ok=true; d.reason="ok";
    return d;
}
