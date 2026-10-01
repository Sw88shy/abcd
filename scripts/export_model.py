"""Export and smoke-test a COCO YOLO26n NCNN model for the C++ application."""
import argparse
import shutil
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--size", type=int, default=320, choices=range(32, 1281, 32), metavar="32..1280")
    parser.add_argument("--destination", type=Path, default=Path("models/yolo26n_ncnn_model"))
    args = parser.parse_args()
    from ultralytics import YOLO
    import ncnn
    import numpy as np

    # Work inside models so downloaded weights and exporter intermediates stay together.
    workspace = Path("models")
    workspace.mkdir(exist_ok=True)
    model = YOLO(str(workspace / "yolo26n.pt"))
    exported = Path(model.export(format="ncnn", imgsz=args.size, batch=1, device="cpu"))
    net = ncnn.Net()
    net.opt.use_vulkan_compute = False
    net.opt.use_packing_layout = False
    net.opt.use_fp16_storage = False
    net.opt.use_fp16_arithmetic = False
    assert net.load_param(str(exported / "model.ncnn.param")) == 0, "Failed loading model parameters"
    assert net.load_model(str(exported / "model.ncnn.bin")) == 0, "Failed loading model weights"
    with net.create_extractor() as ex:
        assert ex.input(net.input_names()[0], ncnn.Mat(np.zeros((3, args.size, args.size), dtype=np.float32))) == 0
        result, output = ex.extract(net.output_names()[0])
        assert result == 0, "NCNN smoke inference failed"
        values = np.asarray(output)
        expected = 21 * (args.size // 32) ** 2
        assert values.shape in ((84, expected), (expected, 84)), f"Unsupported output: {values.shape}"
        assert np.isfinite(values).all(), "Model produced non-finite predictions"
    if exported.resolve() != args.destination.resolve():
        shutil.copytree(exported, args.destination, dirs_exist_ok=True)
    (args.destination / "input-size.txt").write_text(f"{args.size}\n", encoding="ascii")
    print(f"Validated NCNN output {values.shape}. Model ready: {args.destination}")


if __name__ == "__main__":
    main()
