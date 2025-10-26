FROM python:3.11-slim

WORKDIR /app
COPY . /app

RUN pip install flask requests

ENV FLAS_APP=app.py
ENV FLASH_RUN_HOST=0.0.0.0

CMD ["flask", "run", "--host=0.0.0.0", "--port=5000"]
