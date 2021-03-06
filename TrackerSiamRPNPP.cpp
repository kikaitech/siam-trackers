#include "TrackerSiamRPNPP.h"

const int TrackerSiamRPNPP::BACKBONE_USED_LAYERS[BACKBONE_USED_LAYERS_NUM] = { 3, 5, 7 };

void TrackerSiamRPNPP::load_networks_instantly() {
	torch::Tensor z_crop = torch::zeros({ 1, 3, EXEMPLAR_SIZE, EXEMPLAR_SIZE }).cuda();
	torch::List<torch::Tensor> pre_zf = backbone_forward(z_crop);
	torch::List<torch::Tensor> zf = neck_forward(pre_zf);

	torch::Tensor x_crop = torch::zeros({ 1, 3, INSTANCE_SIZE, INSTANCE_SIZE }).cuda();
	torch::List<torch::Tensor> pre_xf = backbone_forward(x_crop);;
	torch::List<torch::Tensor> xf = neck_forward(pre_xf);

	for (int i = 0; i < model.rpns.size(); i++) {
		model.rpns[i].forward({ zf.get(i), xf.get(i) });
	}
}

torch::List<torch::Tensor> TrackerSiamRPNPP::backbone_forward(torch::Tensor tensor) {
	torch::List<torch::Tensor> out;
	int nextUsedLayerIdx = 0;
	for (int i = 0; i < model.backbone.size() && nextUsedLayerIdx < BACKBONE_USED_LAYERS_NUM; i++) {
		tensor = model.backbone[i].forward({ tensor }).toTensor();
		if (i == BACKBONE_USED_LAYERS[nextUsedLayerIdx]) {
			out.push_back(tensor);
			nextUsedLayerIdx++;
		}
	}
	return out;
}

torch::List<torch::Tensor> TrackerSiamRPNPP::neck_forward(torch::List<torch::Tensor> input) {
	return model.neck.forward({ input }).toTensorList();
}

track_result TrackerSiamRPNPP::track(cv::Mat frame) {
	float s_z = calculate_s_z();
	float scale_z = EXEMPLAR_SIZE / s_z;
	int s_x = round(s_z * INSTANCE_SIZE / EXEMPLAR_SIZE);
	torch::Tensor x_crop = get_subwindow(frame, INSTANCE_SIZE, s_x);

	torch::List<torch::Tensor> pre_xf = backbone_forward(x_crop);
	torch::List<torch::Tensor> xf = neck_forward(pre_xf);

	torch::Tensor cls, loc;
	for (int i = 0; i < model.rpns.size(); i++) {
		std::vector<torch::IValue> res = model.rpns[i].forward({ zf.get(i), xf.get(i) }).toTuple()->elements();
		torch::Tensor c = res[0].toTensor().cuda();
		torch::Tensor l = res[1].toTensor().cuda();
		if (cls.numel() > 0) cls += c; else cls = c;
		if (loc.numel() > 0) loc += l; else loc = l;
	}
	cls /= (float)model.rpns.size();
	loc /= (float)model.rpns.size();

	torch::Tensor score = convert_score(cls);
	torch::Tensor pred_bbox = convert_bbox(loc);
	torch::Tensor penalty = get_penalty(scale_z, pred_bbox);
	int best_idx = get_best_idx(penalty, score);
	update_bbox(pred_bbox, best_idx, scale_z, penalty, score, frame.size());

	track_result res;
	res.bbox = rectToRotatedRect(bounding_box);
	return res;
}
