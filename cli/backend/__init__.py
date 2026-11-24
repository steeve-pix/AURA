"""AURA backend modules."""
import warnings

# CLI tensor paths do not use NumPy interoperability. Keep unrelated PyTorch
# warnings visible, including ABI failures and accelerator/runtime problems.
warnings.filterwarnings('ignore',
    message=r"Failed to initialize NumPy: No module named 'numpy'.*",
    category=UserWarning,module=r'torch\._subclasses\.functional_tensor')
