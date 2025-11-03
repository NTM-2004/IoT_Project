package com.iot.parking.model;

import java.time.OffsetDateTime;

import jakarta.persistence.*;

@Entity
@Table(name = "tblPlate")
public class Plate {
	@Id
	@GeneratedValue(strategy = GenerationType.AUTO)
	private Long id;
	private String number;
	private String imagePath;
	private OffsetDateTime timestamp;
	private String type;
	
	public Plate() {}

	public Plate(String number, String imagePath, String type) {
		this.number = number;
		this.imagePath = imagePath;
		this.timestamp = OffsetDateTime.now();
		this.type = type;
	}

	public Long getId() {
		return id;
	}

	public void setId(Long id) {
		this.id = id;
	}

	public String getNumber() {
		return number;
	}

	public void setNumber(String number) {
		this.number = number;
	}

	public String getImagePath() {
		return imagePath;
	}

	public void setImagePath(String imagePath) {
		this.imagePath = imagePath;
	}

	public OffsetDateTime getTimestamp() {
		return timestamp;
	}

	public void setTimestamp(OffsetDateTime timestamp) {
		this.timestamp = timestamp;
	}

	public String getType() {
		return type;
	}

	public void setType(String type) {
		this.type = type;
	}
}
